#include <engine/shared/config.h>
#include <game/client/components/qmclient/weapon_trajectory.h>
#include <gtest/gtest.h>

TEST(QmWeaponTrajectory, SelectsWeaponSpecificBaseColors)
{
	const ColorRGBA GrenadeColor(0.1f, 0.2f, 0.3f, 0.4f);
	const ColorRGBA RifleInnerColor(0.2f, 0.3f, 0.4f, 0.5f);
	const ColorRGBA ShotgunInnerColor(0.3f, 0.4f, 0.5f, 0.6f);

	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_GRENADE, GrenadeColor, RifleInnerColor, ShotgunInnerColor), GrenadeColor);
	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_LASER, GrenadeColor, RifleInnerColor, ShotgunInnerColor), RifleInnerColor);
	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_SHOTGUN, GrenadeColor, RifleInnerColor, ShotgunInnerColor), ShotgunInnerColor);
	EXPECT_EQ(QmWeaponTrajectoryBaseColor(WEAPON_GUN, GrenadeColor, RifleInnerColor, ShotgunInnerColor), ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
}

TEST(QmWeaponTrajectory, InvertsRgbAndPreservesAlpha)
{
	const ColorRGBA Inverted = QmInvertWeaponTrajectoryColor(ColorRGBA(1.0f, 0.25f, 0.0f, 0.35f));
	EXPECT_FLOAT_EQ(Inverted.r, 0.0f);
	EXPECT_FLOAT_EQ(Inverted.g, 0.75f);
	EXPECT_FLOAT_EQ(Inverted.b, 1.0f);
	EXPECT_FLOAT_EQ(Inverted.a, 0.35f);
}

TEST(QmWeaponTrajectory, UsesLaserLineStyleForGun)
{
	EXPECT_TRUE(QmWeaponTrajectoryUsesLineStyle(WEAPON_GUN));
	EXPECT_TRUE(QmWeaponTrajectoryUsesLineStyle(WEAPON_SHOTGUN));
	EXPECT_TRUE(QmWeaponTrajectoryUsesLineStyle(WEAPON_LASER));
	EXPECT_FALSE(QmWeaponTrajectoryUsesLineStyle(WEAPON_GRENADE));
}

TEST(QmWeaponTrajectory, PistolGuideToggleOnlyAffectsGun)
{
	EXPECT_EQ(DefaultConfig::QmWeaponTrajectoryGun, 1);
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_GUN, true));
	EXPECT_FALSE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_GUN, false));
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_GRENADE, false));
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_SHOTGUN, false));
	EXPECT_TRUE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_LASER, false));
	EXPECT_FALSE(QmWeaponTrajectoryEnabledForWeapon(WEAPON_HAMMER, true));
}

TEST(QmWeaponTrajectory, NinjaPredictionRequiresEnabledLocalNinja)
{
	EXPECT_EQ(DefaultConfig::QmWeaponTrajectoryNinja, 0);
	EXPECT_TRUE(QmWeaponTrajectoryNinjaEnabled(WEAPON_NINJA, true, true));
	EXPECT_FALSE(QmWeaponTrajectoryNinjaEnabled(WEAPON_NINJA, false, true));
	EXPECT_FALSE(QmWeaponTrajectoryNinjaEnabled(WEAPON_NINJA, true, false));
	EXPECT_FALSE(QmWeaponTrajectoryNinjaEnabled(WEAPON_GUN, true, true));
}

TEST(QmWeaponTrajectory, RespectsWeaponSpecificPlayerHitDisables)
{
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_GUN, true, false, false));
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_GRENADE, true, false, false));
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_LASER, false, true, false));
	EXPECT_FALSE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_SHOTGUN, false, false, true));
	EXPECT_TRUE(QmWeaponTrajectoryCanHitOtherPlayers(WEAPON_GUN, false, true, true));
}

TEST(QmWeaponTrajectory, AcceptsFrontLayerTeleGunWallsForSupportedWeapons)
{
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, TILE_ALLOW_TELE_GUN, 0, 0));
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GRENADE, TILE_ALLOW_BLUE_TELE_GUN, 0, 0));
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_LASER, TILE_ALLOW_TELE_GUN, 0, 0));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_SHOTGUN, TILE_ALLOW_TELE_GUN, 0, 0));
}

TEST(QmWeaponTrajectory, RequiresTheCurrentWeaponTeleGunCapability)
{
	EXPECT_TRUE(QmWeaponTrajectoryHasTeleGun(WEAPON_GUN, true, false, false));
	EXPECT_TRUE(QmWeaponTrajectoryHasTeleGun(WEAPON_GRENADE, false, true, false));
	EXPECT_TRUE(QmWeaponTrajectoryHasTeleGun(WEAPON_LASER, false, false, true));
	EXPECT_FALSE(QmWeaponTrajectoryHasTeleGun(WEAPON_SHOTGUN, true, true, true));
	EXPECT_FALSE(QmWeaponTrajectoryHasTeleGun(WEAPON_GUN, false, true, true));
}

TEST(QmWeaponTrajectory, AppliesSwitchLayerWeaponDelay)
{
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, 0, TILE_ALLOW_TELE_GUN, 0));
	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, 0, TILE_ALLOW_TELE_GUN, 1));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GUN, 0, TILE_ALLOW_TELE_GUN, 2));

	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GRENADE, 0, TILE_ALLOW_BLUE_TELE_GUN, 2));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_GRENADE, 0, TILE_ALLOW_BLUE_TELE_GUN, 3));

	EXPECT_TRUE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_LASER, 0, TILE_ALLOW_TELE_GUN, 3));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_LASER, 0, TILE_ALLOW_TELE_GUN, 1));
	EXPECT_FALSE(QmWeaponTrajectoryIsTeleGunWall(WEAPON_SHOTGUN, 0, TILE_ALLOW_TELE_GUN, 0));
}
