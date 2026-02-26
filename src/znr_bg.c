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

/*
 * For zoned devices, this function must only be called after zones
 * have been mapped to @bg. If no zones exists in @bg, fallback to the
 * filesystem to see if a writepointer exists to determine the blockgroup type.
 */
static int znr_bg_init_bg(struct znr_bg *bg)
{
	struct blk_zone *zone;

	if (!bg)
		return -EINVAL;

	/* No zones means this is regular block device */
	if (!bg->nr_zones) {
		if (bg->fs_has_wp) {
			bg->type = BG_SEQUENTIAL;
		} else {
			bg->type = BG_CONVENTIONAL;
			bg->wp_sector = 0;
		}
		return 0;
	}

	zone = bg->zones[0];
	if (!zone || !bg->nr_zones)
		return -EINVAL;

	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		bg->type = BG_CONVENTIONAL;
		bg->wp_sector = 0;
		break;
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
		bg->type = BG_SEQUENTIAL;
		if (bg->sector > zone->wp) {
			fprintf(stderr, "Zone writepointer not in zone\n");
			return -EINVAL;
		}
		bg->wp_sector = zone->wp - bg->sector;
		break;
	default:
		fprintf(stderr, "Unsupported blockgroup type: %u!\n",
			zone->type);
		return -ENOTSUP;
	}

	return 0;
}

static int znr_bg_map_zones_to_blockgroups(struct znr_bg *blockgroups,
					   unsigned int nr_blockgroups,
					   struct blk_zone *zones,
					   unsigned int nr_zones,
					   unsigned int zone_sectors)
{
	unsigned long bg_sector_end, zone_sector_end;
	unsigned int max_zones_per_bg, bg_zone_idx, j, i, zone_start_idx = 0;
	int ret;

	znr_verbose("Mapping %u zones to %u blockgroups\n", nr_zones,
		    nr_blockgroups);

	if (!blockgroups || !zones)
		return -EINVAL;

	if (nr_zones < nr_blockgroups)
		return -EINVAL;

	if (nr_blockgroups > znr.nr_blockgroups)
		return -EINVAL;

	if (nr_zones > znr.nr_zones)
		return -EINVAL;

	for (i = 0; i < nr_blockgroups; ++i) {
		/*
		 * Max zones mapped to a blockgroup plus some padding for
		 * overlap between blockgroups (e.g XFS allocation groups).
		 */
		max_zones_per_bg =
			(blockgroups[i].nr_sectors / zone_sectors) + 4;
		blockgroups[i].nr_zones = max_zones_per_bg;
		blockgroups[i].zones =
			calloc(max_zones_per_bg, sizeof(struct blk_zone *));
		if (!blockgroups[i].zones) {
			fprintf(stderr, "No memory for blockgroup zone array\n");
			ret = -ENOMEM;
			goto out_free;
		}

		bg_sector_end = blockgroups[i].sector +
				blockgroups[i].nr_sectors;
		bg_zone_idx = 0;

		/*
		 * Start from where we left off, but check the last zone back.
		 * For conventional zones, blockgroups may overlap zones.
		 */
		j = (zone_start_idx > 1) ? zone_start_idx - 1 : 0;
		for (; j < nr_zones; ++j) {
			zone_sector_end = zones[j].start + zones[j].len;

			/* Skip zones that end before this blockgroup starts */
			if (zone_sector_end <= blockgroups[i].sector) {
				zone_start_idx = j + 1;
				continue;
			}

			/*
			 * Stop checking zones that start at or after this
			 * blockgroup ends
			 */
			if (zones[j].start >= bg_sector_end)
				break;

			/* This zone overlaps with the i'th blockgroup */
			blockgroups[i].zones[bg_zone_idx] = &zones[j];
			bg_zone_idx++;
			if (bg_zone_idx > max_zones_per_bg) {
				fprintf(stderr,
					"Too many zones in blockgroup[%u]:  [%u/%u]\n",
					i, bg_zone_idx, max_zones_per_bg);
				ret = -EINVAL;
				goto out_free;
			}
			blockgroups[i].nr_zones = bg_zone_idx;
		}

		if (!blockgroups[i].nr_zones) {
			fprintf(stderr,
				"No zones mapped to blockgroup %u\n", i);
			ret = -EINVAL;
			goto out_free;
		}

		ret = znr_bg_init_bg(&blockgroups[i]);
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

	if (!dev->is_zoned) {
		ret = znr_fs_report_blockgroups(&blockgroups[blockgroup_no],
						blockgroup_no,
						nr_blockgroups);
		if (ret < 0)
			return ret;
		nr_blockgroups = ret;
		for (unsigned int i = 0; i < nr_blockgroups; i++) {
			ret = znr_bg_init_bg(&blockgroups[i]);
			if (ret) {
				fprintf(stderr, "Failed to init blockgroup: %u\n",
					i);
				return ret;
			}
		}
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

	ret = znr_bg_to_zno(dev, blockgroups, &blockgroups[nr_blockgroups - 1],
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

	ret = znr_bg_map_zones_to_blockgroups(blockgroups, nr_blockgroups,
					      &zones[start_zone_no],
					      nr_zones, dev->zone_sectors);
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

