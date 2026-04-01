/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPDX-FileCopyrightText: 2026 Western Digital Corporation or its affiliates.
 */
#ifndef ZNR_BG_H
#define ZNR_BG_H

/*
 * Block group information.
 */
struct znr_bg {
        /* Starting sector */
	unsigned long sector;

	/* Number of sectors */
	unsigned long nr_sectors;

	/* Write pointer sector offset within this blockgroup */
	unsigned long wp_sector;

	unsigned int flags;

	/* Zones in this block group */
	struct blk_zone **zones;
	unsigned long nr_zones;
};

enum znr_bg_flags {
	/* Set if the backing zone has a write pointer */
	ZNR_BG_HAS_ZONE_WP = (1U << 0),
	/* Set if the filesystem provided a write pointer */
	ZNR_BG_HAS_FS_WP   = (1U << 1),
	ZNR_BG_HAS_WP	   = ZNR_BG_HAS_ZONE_WP | ZNR_BG_HAS_FS_WP,
	/* Set if the blockgroup is fully written */
	ZNR_BG_FULL	   = (1U << 2),
	/* Blockgroup has backing zone information (Zoned block devices) */
	ZNR_BG_HAS_ZONE_INFO = (1U << 3),
};

int znr_bg_get_blockgroups(struct znr_bg **blockgroups,
			   unsigned int *nr_blockgroups);

int znr_bg_refresh(struct znr_device *dev, struct blk_zone *zones,
		   unsigned int max_zones, struct znr_bg *blockgroups,
		   unsigned int blockgroup_num, unsigned int nr_blockgroups);

void znr_bgs_destroy(struct znr_bg *blockgroups, unsigned int nr_blockgroups);

#endif /* ZNR_BG_H */
