/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SPDX-FileCopyrightText: 2026 Western Digital Corporation or its affiliates.
 */
#ifndef ZNR_BG_H
#define ZNR_BG_H

/*
 * @BG_FS_HAS_WP: The filesystem reports a writepointer.
 */
enum bg_fs_flags {
	BG_FS_HAS_WP	= 0x1,
};

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

	/* Blockzone flags */
	unsigned int flags;

	/* Filesystem specific flags */
	unsigned int fs_flags;

	/* Zones in this block group */
	struct blk_zone **zones;
	unsigned long nr_zones;
};

int znr_bg_get_blockgroups(struct znr_bg **blockgroups,
			   unsigned int *nr_blockgroups);

int znr_bg_refresh(struct znr_device *dev, struct blk_zone *zones,
		   unsigned int max_zones, struct znr_bg *blockgroups,
		   unsigned int blockgroup_num, unsigned int nr_blockgroups);

void znr_bgs_destroy(struct znr_bg *blockgroups, unsigned int nr_blockgroups);

#endif /* ZNR_BG_H */
