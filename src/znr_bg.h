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
	 * The device indicated write pointer sector offset within this
	 * blockgroup. Valid if ZNR_BG_HAS_DEV_ZONE_WP is set in flags.
	 */
	unsigned long dev_zone_wp;

	/*
	 * The current write pointer offset within this blockgroup used by the
	 * FS to issue writes. Valif if ZNR_BG_HAS_FS_WP is set in flags.
	 */
	unsigned long fs_wp;

	unsigned int flags;

	/* Zones in this block group */
	struct blk_zone **zones;
	unsigned long nr_zones;
};

enum znr_bg_flags {
	/*
	 * Set for a blockgroup backed by a sequential write required zone of
	 * a zoned device
	 */
	ZNR_BG_HAS_DEV_ZONE_WP	= (1U << 0),
	/* Set if the filesystem provides a write pointer */
	ZNR_BG_HAS_FS_WP	= (1U << 1),
	ZNR_BG_MAPPING_INITIALIZED = (1U << 2),
};

/*
 * Helper function for zone backed blockgroups to check if the zone is
 * fully written. Must only be called for zone backed blockgroups
 */
static inline bool znr_bg_dev_zone_full(struct znr_bg *bg)
{
	if (!bg->nr_zones || !bg->zones)
		return false;

	return bg->zones[0]->cond == BLK_ZONE_COND_FULL;
}

/* Helper function that checks if the blockgroup is fully written */
static inline bool znr_bg_full(struct znr_bg *bg)
{
	/* If this is a zoned device check the zone condition */
	if (bg->nr_zones && bg->zones)
		return znr_bg_dev_zone_full(bg);

	return bg->fs_wp >= bg->nr_sectors;
}

static inline bool znr_bg_has_wp(struct znr_bg *bg)
{
	return bg->flags & (ZNR_BG_HAS_DEV_ZONE_WP | ZNR_BG_HAS_FS_WP);
}

int znr_bg_get_blockgroups(struct znr_bg **blockgroups,
			   unsigned int *nr_blockgroups);

int znr_bg_refresh(struct znr_device *dev, struct blk_zone *zones,
		   unsigned int max_zones, struct znr_bg *blockgroups,
		   unsigned int blockgroup_num, unsigned int nr_blockgroups);

void znr_bgs_destroy(struct znr_bg *blockgroups, unsigned int nr_blockgroups);

#endif /* ZNR_BG_H */
