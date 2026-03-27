// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SPDX-FileCopyrightText: 2026 Western Digital Corporation or its affiliates.
 *
 * Authors: Wilfred Mallawa (wilfred.mallawa@wdc.com)
 */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "znr.h"
#include "znr_bg.h"
#include "znr_fs.h"

int znr_bg_get_blockgroups(struct znr_bg **blockgroups,
			   unsigned int *nr_blockgroups)
{
	return znr_fs_get_blockgroups(blockgroups, nr_blockgroups);
}

void znr_bgs_destroy(struct znr_bg *blockgroups, unsigned int nr_blockgroups)
{
	unsigned int i;

	for (i = 0; i < nr_blockgroups; i++) {
		free(blockgroups[i].zones);
		blockgroups[i].zones = NULL;
	}
}

static int znr_bg_get_zone_mapping(struct znr_bg *blockgroup,
				   struct blk_zone *zones,
				   unsigned int *zone_idx,
				   unsigned int nr_zones,
				   unsigned int zone_sectors)
{
	unsigned long bg_sector_end, zone_sector_end;
	unsigned int j, max_zones_per_bg, bg_zone_idx = 0;
	int ret = 0;

	if (!blockgroup || !zones || !nr_zones || !zone_idx)
		return -EINVAL;

	if (*zone_idx >= nr_zones)
		return -EINVAL;

	/*
	 * Max zones mapped to a blockgroup plus some padding for
	 * overlap between blockgroups (e.g XFS allocation groups).
	 */
	max_zones_per_bg = (blockgroup->nr_sectors / zone_sectors) + 4;
	blockgroup->zones =
		calloc(max_zones_per_bg, sizeof(struct blk_zone *));
	if (!blockgroup->zones) {
		fprintf(stderr, "No memory for blockgroup zone array\n");
		return -ENOMEM;
	}

	bg_sector_end = blockgroup->sector + blockgroup->nr_sectors;
	bg_zone_idx = 0;

	/*
	 * Start from the previous zone if possible to see if there was
	 * any overlap.
	 */
	j = *zone_idx > 0 ? *zone_idx - 1 : 0;
	for (; j < nr_zones; ++j) {
		zone_sector_end = zones[j].start + zones[j].len;

		/* Skip zones that end before this blockgroup starts */
		if (zone_sector_end <= blockgroup->sector)
			continue;

		/*
		 * Stop checking zones that start at or after this blockgroup
		 * ends
		 */
		if (zones[j].start >= bg_sector_end)
			break;

		/* This zone spans the blockgroup */
		blockgroup->zones[bg_zone_idx] = &zones[j];
		bg_zone_idx++;
		if (bg_zone_idx > max_zones_per_bg) {
			fprintf(stderr,
				"Too many zones in blockgroup: [%u/%u]\n",
				bg_zone_idx, max_zones_per_bg);
			free(blockgroup->zones);
			blockgroup->zones = NULL;
			return -EINVAL;
		}
	}

	if (!bg_zone_idx) {
		fprintf(stderr, "No zones in blockgroup\n");
		free(blockgroup->zones);
		blockgroup->zones = NULL;
		return -EINVAL;
	}

	/*
	 * If the filesystem provided a writepointer, use that instead
	 * as the device write pointer always trails the in-memory
	 * allocation pointer a bit when I/O is pending
	 */
	if (!blockgroup->fs_has_wp && blockgroup->type == BG_SEQ_WRITE)
		blockgroup->wp_sector = blockgroup->zones[0]->wp -
					   blockgroup->sector;
	else
		blockgroup->wp_sector = 0;

	blockgroup->nr_zones = bg_zone_idx;
	*zone_idx = j;
	return ret;
}

static int znr_bg_get_zone_info(struct znr_bg *blockgroups,
				unsigned int nr_blockgroups,
				struct blk_zone *zones,
				unsigned int nr_zones,
				unsigned int zone_sectors)
{
	unsigned int i, zone_idx = 0;
	int ret = 0;


	if (!blockgroups || !zones)
		return -EINVAL;

	if (nr_zones < nr_blockgroups)
		return -EINVAL;

	if (nr_blockgroups > znr.nr_blockgroups)
		return -EINVAL;

	if (nr_zones > znr.nr_zones)
		return -EINVAL;

	znr_verbose("Getting zone info for %u blockgroup starting at sector: 0x%lx\n",
		    nr_blockgroups, blockgroups[0].sector);

	for (i = 0; i < nr_blockgroups; i++) {
		if (zone_idx >= nr_zones) {
			ret = -EINVAL;
			goto out_free;
		}

		ret = znr_bg_get_zone_mapping(&blockgroups[i], zones,
					      &zone_idx, nr_zones,
					      zone_sectors);
		if (ret)
			goto out_free;
	}

	return 0;
out_free:
	znr_bgs_destroy(blockgroups, i);
	return ret;
}

static int znr_bg_to_zno(struct znr_device *dev,
			 struct znr_bg *blockgroup_start,
			 struct znr_bg *blockgroup_end,
			 unsigned int *zno_start, unsigned int *zno_end)
{
	unsigned int start_zone_no, end_zone_no;

	if (!blockgroup_start || !blockgroup_end || !zno_start || !zno_end)
		return -EINVAL;

	if (blockgroup_start->sector > blockgroup_end->sector)
		return -EINVAL;

	/*
	 * A blockgroup could use multiple zones on the device, in which case,
	 * we need to get the actual zone numbers on the device to do a zone
	 * report
	 */
	start_zone_no = blockgroup_start->sector / dev->zone_sectors;
	end_zone_no = (blockgroup_end->sector + blockgroup_end->nr_sectors) /
		dev->zone_sectors;

	if (end_zone_no > dev->nr_zones) {
		fprintf(stderr, "Invalid zone in blockgroup\n");
		return -EINVAL;
	}

	*zno_start = start_zone_no;
	*zno_end = end_zone_no;

	return 0;
}

static int znr_bg_report(struct znr_device *dev, struct blk_zone *zones,
			 unsigned int max_zones, struct znr_bg *blockgroups,
			 unsigned int blockgroup_no,
			 unsigned int nr_blockgroups)
{
	unsigned int last_zone_no, start_zone_no, nr_zones;
	unsigned long max_sector;
	int ret;

	if (!blockgroups || !nr_blockgroups ||
	    blockgroup_no + nr_blockgroups > znr.nr_blockgroups)
		return -EINVAL;

	/*
	 * Filesystem would have provided the necessary information, but
	 * we need to report here to see if the writepointer has changed
	 */
	if (!dev->is_zoned) {
		ret = znr_fs_report_blockgroups(&blockgroups[blockgroup_no],
						blockgroup_no,
						nr_blockgroups);
		if (ret < 0)
			return ret;

		nr_blockgroups = ret;
		return nr_blockgroups;
	}

	if (!dev || !zones || !max_zones || max_zones > dev->nr_zones)
		return -EINVAL;

	znr_verbose("Do blockgroup reports from group %u, %u groups\n",
		    blockgroup_no, nr_blockgroups);

	/* The last sector in this set of blockgroups */
	max_sector = blockgroups[nr_blockgroups - 1].sector +
		     blockgroups[nr_blockgroups - 1].nr_sectors;
	if (max_sector > dev->nr_sectors) {
		fprintf(stderr, "Sector out of bounds: sector: %ld | max: %lld\n",
			max_sector, dev->nr_sectors);
		return -EINVAL;
	}

	ret = znr_bg_to_zno(dev, &blockgroups[blockgroup_no],
			    &blockgroups[blockgroup_no + (nr_blockgroups - 1)],
			    &start_zone_no, &last_zone_no);
	if (ret)
		return ret;

	nr_zones = last_zone_no - start_zone_no;
	if (!nr_zones || nr_zones > max_zones)
		return -EINVAL;

	/* Do zone report */
	ret = znr_dev_report_zones(dev, start_zone_no,
				   &zones[start_zone_no], nr_zones);
	if ((unsigned int)ret != nr_zones)
		return -EINVAL;

	ret = znr_bg_get_zone_info(&blockgroups[blockgroup_no],
				   nr_blockgroups,
				   &zones[start_zone_no], nr_zones,
				   dev->zone_sectors);
	if (ret)
		return ret;

	return nr_blockgroups;
}

int znr_bg_refresh(struct znr_device *dev, struct blk_zone *zones,
		   unsigned int max_zones, struct znr_bg *blockgroups,
		   unsigned int blockgroup_no, unsigned int nr_blockgroups)
{
	znr_verbose("Refreshing %u blockgroups, starting at blockgroup %u\n",
		    nr_blockgroups, blockgroup_no);

	return znr_bg_report(dev, zones, max_zones, blockgroups,
			     blockgroup_no, nr_blockgroups);
}

