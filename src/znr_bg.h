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

	/*
	 * Write pointer sector offset within this blockgroup, set from
	 * the device backing zone for this blockgroup. Valid if
	 * ZNR_BG_HAS_DEV_ZONE_WP is set in flags.
	 */
	unsigned long dev_zone_wp_sector;

	/*
	 * Write pointer sector offset within this blockgroup
	 * set by the filesystem, Valid if ZNR_BG_HAS_FS_WP is set in flags.
	 */
	unsigned long fs_wp_sector;

	unsigned int flags;

	/* Zones in this block group */
	struct blk_zone **zones;
	unsigned long nr_zones;
};

enum znr_bg_flags {
	/* Set if the backing zone on device has a write pointer */
	ZNR_BG_HAS_DEV_ZONE_WP	= (1U << 0),
	/* Set if the filesystem provided a write pointer */
	ZNR_BG_HAS_FS_WP	= (1U << 1),
	/* Set if the blockgroup is fully written */
	ZNR_BG_FULL		= (1U << 2),
	/*
	 * Blockgroup has backing zone information (Zoned block devices),
	 * valid only for the local context, so should not be sent over the
	 * network during blockgroup reports
	 */
	ZNR_BG_HAS_ZONE_INFO	= (1U << 3),
};

static inline bool znr_bg_has_wp(struct znr_bg *bg)
{
	return (bg->flags & (ZNR_BG_HAS_DEV_ZONE_WP | ZNR_BG_HAS_FS_WP)) != 0;
}

int znr_bg_get_blockgroups(struct znr_bg **blockgroups,
			   unsigned int *nr_blockgroups);

int znr_bg_refresh(struct znr_device *dev, struct blk_zone *zones,
		   unsigned int max_zones, struct znr_bg *blockgroups,
		   unsigned int blockgroup_num, unsigned int nr_blockgroups);

void znr_bgs_destroy(struct znr_bg *blockgroups, unsigned int nr_blockgroups);

#endif /* ZNR_BG_H */
