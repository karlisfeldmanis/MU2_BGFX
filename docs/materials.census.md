# The material census

Written by `tools/matcheck.py`. Every material slot in `assets/`, what library entry
it claims, and what its baked ORM actually carries.

636 slots on 318 models, 465 cooked textures. 147 slots (23%) resolve to a library material; 424 do not.

## Unruled slots

| model | slot | triangles | area | ORM rough | ORM metal |
|---|---|---:|---:|---:|---:|
| Tree02 | Tree_a | 164 | 172.48 | 0.900 | 0.000 |
| Ship01 | TileGround03 | 284 | 164.19 | 0.730 | 0.000 |
| Tree01 | tree_a | 144 | 154.12 | 0.900 | 0.000 |
| Object50 | gatewall041 | 959 | 147.35 | 0.731 | 0.000 |
| House05 | tile_wood02 | 228 | 74.01 | 0.818 | 0.000 |
| House03 | tile_ston04 | 77 | 72.67 | 0.902 | 0.000 |
| House01 | tile_ston04 | 65 | 62.43 | 0.898 | 0.000 |
| Object25 | treea00 | 606 | 55.96 | 0.880 | 0.000 |
| Tree02 | tree | 606 | 55.94 | 0.880 | 0.000 |
| Straw02 | grass_01 | 352 | 55.63 | 0.900 | 0.000 |
| Object39 | so_bob03 | 30 | 52.08 | 0.726 | 0.000 |
| Object21 | wood03 | 264 | 47.42 | 1.000 | 0.000 |
| DoungeonGate01 | copra_gate | 333 | 45.92 | 0.736 | 0.000 |
| Object31 | wood002 | 230 | 44.40 | 0.883 | 0.000 |
| HouseEtc02 | tile_ston04 | 46 | 44.05 | 0.896 | 0.000 |
| House05 | tile_wood01 | 20 | 43.01 | 0.818 | 0.000 |
| House03 | tile_ston06 | 192 | 42.91 | 0.818 | 0.000 |
| Ship01 | ship01 | 34 | 40.77 | 0.817 | 0.000 |
| Object35 | song_bria01 | 52 | 40.72 | 0.731 | 0.000 |
| Tree11 | tree_06 | 144 | 40.49 | 0.900 | 0.000 |
| Bridge01 | bridge_01 | 194 | 38.77 | 0.726 | 0.000 |
| Object99 | so_jgwall01 | 22 | 38.68 | 0.733 | 0.000 |
| StoneGolem01 | ston1 | 460 | 34.94 | 0.862 | 0.000 |
| Object31 | wood001 | 76 | 32.82 | 0.722 | 0.000 |
| Ship01 | ship03 | 62 | 31.08 | 0.821 | 0.000 |
| StoneStatue02 | stone_statue01 | 107 | 31.03 | 0.734 | 0.000 |
| MerchantAnimal01 | merchant_moster_a01 | 540 | 30.28 | 0.700 | 0.000 |
| HouseEtc02 | c_wall04 | 25 | 29.68 | 0.731 | 0.000 |
| StoneMuWall01 | c_wall04 | 56 | 29.22 | 0.726 | 0.000 |
| Tree13 | tree_05 | 120 | 28.97 | 0.900 | 0.000 |
| Tree12 | tree_04 | 120 | 28.97 | 0.900 | 0.000 |
| House04 | tile_wood01 | 117 | 28.43 | 0.812 | 0.000 |
| Object40 | htht01 | 803 | 28.24 | 0.722 | 0.000 |
| StoneWall03 | tile_01 | 50 | 26.93 | 0.730 | 0.000 |
| StoneMuWall01 | tile_02 | 82 | 25.81 | 0.826 | 0.000 |
| StoneWall01 | tile_02 | 82 | 25.47 | 0.826 | 0.000 |
| StoneWall02 | tile_02 | 82 | 25.47 | 0.826 | 0.000 |
| HouseWall06 | tile_wood02 | 26 | 25.41 | 0.820 | 0.000 |
| Object99 | so_jgwall02 | 448 | 25.13 | 0.728 | 0.000 |
| HouseEtc03 | steel_barred_a | 12 | 23.55 | 0.400 | 0.580 |
| Ship01 | ship07 | 188 | 23.31 | 0.816 | 0.000 |
| Object15 | so_jghwall02 | 362 | 23.19 | 0.732 | 0.000 |
| HouseWall05 | tile_wood02 | 26 | 22.67 | 0.821 | 0.000 |
| Object112 | drogon_001 | 732 | 22.59 | 0.731 | 0.000 |
| Stone01 | ston01 | 140 | 22.19 | 0.860 | 0.000 |
| Tree08 | tree_01 | 176 | 21.75 | 0.879 | 0.000 |
| House01 | tile_ston06 | 104 | 21.16 | 0.816 | 0.000 |
| House03 | tile_ston07 | 12 | 21.02 | 0.822 | 0.000 |
| Carriage04 | grass_01 | 132 | 20.86 | 0.900 | 0.000 |
| Straw01 | grass_01 | 132 | 20.86 | 0.900 | 0.000 |
| StoneMuWall03 | c_wall04 | 38 | 20.58 | 0.727 | 0.000 |
| StoneMuWall04 | c_wall04 | 38 | 20.58 | 0.727 | 0.000 |
| Object103 | so_jgwall04 | 176 | 20.11 | 0.725 | 0.000 |
| Tree01 | tree | 268 | 19.93 | 0.881 | 0.000 |
| Object35 | so_archb02 | 110 | 19.91 | 0.862 | 0.000 |
| Object21 | flower6 | 284 | 19.54 | 0.900 | 0.000 |
| Hanging01 | horse_drawn_01 | 162 | 19.52 | 0.814 | 0.000 |
| StoneWall01 | tile_01 | 88 | 19.48 | 0.731 | 0.000 |
| StoneWall02 | tile_01 | 88 | 19.48 | 0.731 | 0.000 |
| House04 | tile_ston04 | 48 | 19.20 | 0.897 | 0.000 |
| Stone02 | ston01 | 125 | 18.90 | 0.864 | 0.000 |
| Tree10 | tree_07 | 480 | 18.09 | 0.900 | 0.000 |
| Storage01 | mu2 | 1512 | 17.94 | 0.780 | 0.000 |
| Waterspout01 | reagon_waterspout | 417 | 17.41 | 0.729 | 0.000 |
| Carriage04 | horse_drawn_01 | 258 | 17.00 | 0.811 | 0.000 |
| Carriage02 | horse_drawn_01 | 258 | 17.00 | 0.811 | 0.000 |
| House04 | tile_02 | 100 | 16.59 | 0.821 | 0.000 |
| Ship01 | ship04 | 58 | 16.38 | 0.722 | 0.000 |
| HouseWall04 | tile_wood01 | 12 | 16.26 | 0.821 | 0.000 |
| Object98 | so_jgtree02 | 28 | 16.13 | 0.900 | 0.000 |
| House04 | tile_wood03 | 31 | 15.70 | 0.903 | 0.000 |
| StoneWall06 | tile_01 | 20 | 15.56 | 0.727 | 0.000 |
| Object29 | treea00 | 411 | 15.44 | 0.881 | 0.000 |
| House01 | tile_ston05 | 2 | 14.98 | 0.910 | 0.000 |
| Object111 | door_01 | 1311 | 14.91 | 0.724 | 0.000 |
| HouseEtc02 | tile_wood03 | 16 | 14.88 | 0.904 | 0.000 |
| House03 | tile_ston05 | 2 | 14.74 | 0.894 | 0.000 |
| StoneStatue01 | stone_statue02 | 92 | 14.72 | 0.732 | 0.000 |
| Waterspout01 | stone_statue02 | 72 | 13.69 | 0.735 | 0.000 |
| SteelStatue01 | tombstone_big | 66 | 13.60 | 0.732 | 0.000 |
| Carriage01 | horse_drawn_01 | 270 | 13.27 | 0.807 | 0.000 |
| HouseWall04 | tile_wood02 | 32 | 13.18 | 0.819 | 0.000 |
| Object33 | ston001 | 80 | 12.91 | 0.863 | 0.000 |
| Tree03 | tree_03 | 273 | 12.88 | 0.880 | 0.000 |
| Tree04 | tree_03 | 273 | 12.77 | 0.880 | 0.000 |
| StoneWall03 | horse_drawn_01 | 233 | 12.73 | 0.815 | 0.000 |
| StoneMuWall04 | horse_drawn_01 | 233 | 12.73 | 0.815 | 0.000 |
| Tent01 | tile_house01 | 26 | 12.64 | 0.827 | 0.000 |
| Tomb01 | grave_02 | 44 | 11.82 | 0.738 | 0.000 |
| Tree09 | tree_07 | 240 | 11.75 | 0.900 | 0.000 |
| Object19 | flower7 | 524 | 11.63 | 0.875 | 0.000 |
| HouseWall05 | tile_wood03 | 4 | 11.51 | 0.899 | 0.000 |
| HouseEtc02 | tile_wood02 | 2 | 10.87 | 0.835 | 0.000 |
| HouseEtc02 | tile_ston01 | 8 | 10.73 | 0.729 | 0.000 |
| HouseEtc02 | horse_drawn_01 | 94 | 10.56 | 0.819 | 0.000 |
| Object65 | statueddown82 | 697 | 10.31 | 0.728 | 0.000 |
| ForestMonster01 | mu2 | 512 | 10.31 | 0.620 | 0.000 |
| Object18 | flower7 | 1024 | 10.09 | 0.875 | 0.000 |
| BridgeStone01 | tile_ston06 | 52 | 10.06 | 0.822 | 0.000 |
| Well02 | well__timber | 67 | 9.94 | 0.808 | 0.000 |
| Stone03 | ston01 | 80 | 9.94 | 0.858 | 0.000 |
| Object97 | so_jgtree02 | 20 | 9.90 | 0.900 | 0.000 |
| Well02 | well | 56 | 9.77 | 0.837 | 0.000 |
| Grass02 | tree_08 | 120 | 9.61 | 0.900 | 0.000 |
| Grass01 | tree_08 | 120 | 9.61 | 0.900 | 0.000 |
| StoneWall03 | tile_03 | 10 | 9.39 | 0.727 | 0.000 |
| Tree12 | tree_01 | 273 | 9.18 | 0.879 | 0.000 |
| Tree13 | tree_01 | 273 | 9.13 | 0.879 | 0.000 |
| Grass03 | tree_01 | 96 | 9.03 | 0.900 | 0.000 |
| Tomb02 | grave_01 | 40 | 9.00 | 0.734 | 0.000 |
| StoneMuWall02 | c_wall04 | 12 | 8.90 | 0.725 | 0.000 |
| Waterspout01 | ston02 | 90 | 8.89 | 1.000 | 0.000 |
| Object65 | statuedup72 | 902 | 8.83 | 0.730 | 0.000 |
| SteelWall02 | steel_barred_a | 8 | 8.80 | 0.722 | 1.000 |
| Object30 | treea00 | 214 | 8.70 | 0.881 | 0.000 |
| Object42 | u2u3 | 156 | 8.69 | 1.000 | 0.000 |
| House02 | drum | 26 | 8.39 | 0.819 | 0.000 |
| Object107 | so_jgwall03 | 110 | 8.15 | 0.724 | 0.000 |
| Object32 | ston001 | 80 | 8.11 | 0.860 | 0.000 |
| Tent01 | tile_ston06 | 40 | 7.98 | 0.829 | 0.000 |
| Grass04 | tree_01 | 96 | 7.94 | 0.900 | 0.000 |
| Object30 | so_archb03 | 45 | 7.92 | 0.724 | 0.000 |
| HouseWall01 | tile_wood01 | 8 | 7.79 | 0.813 | 0.000 |
| Smith01 | mu2 | 730 | 7.72 | 0.701 | 0.000 |
| Spider01 | mu2 | 168 | 7.69 | 0.620 | 0.000 |
| MerchantAnimal02 | merchant_moster_b01 | 484 | 7.35 | 0.700 | 0.000 |
| House05 | ston02 | 28 | 7.33 | 1.000 | 0.000 |
| Grass03 | tree_02 | 32 | 7.01 | 0.900 | 0.000 |
| HouseWall02 | tile_wood01 | 26 | 7.01 | 0.823 | 0.000 |
| Tree06 | tree_02 | 89 | 6.81 | 0.883 | 0.000 |
| StoneWall01 | tile_03 | 20 | 6.80 | 0.725 | 0.000 |
| StoneWall02 | tile_03 | 20 | 6.80 | 0.725 | 0.000 |
| Object98 | so_jgtree01 | 404 | 6.72 | 0.879 | 0.000 |
| Object19 | wood01 | 197 | 6.58 | 0.724 | 0.000 |
| HouseWall02 | tile_wood02 | 20 | 6.40 | 0.814 | 0.000 |
| HouseWall01 | tile_wood02 | 20 | 6.38 | 0.816 | 0.000 |
| Cannon01 | horse_drawn_01 | 175 | 6.16 | 0.770 | 0.491 |
| Grass04 | tree_02 | 32 | 6.15 | 0.900 | 0.000 |
| Object28 | treea01 | 160 | 6.10 | 0.900 | 0.000 |
| Furniture01 | bookshelf | 32 | 6.09 | 0.722 | 0.000 |
| Furniture02 | bookshelf | 32 | 6.09 | 0.722 | 0.000 |
| BeetleMonster01 | bug | 430 | 6.07 | 0.620 | 0.000 |
| StoneMuWall03 | c_wall06 | 14 | 5.99 | 0.727 | 0.000 |
| StoneMuWall04 | c_wall06 | 14 | 5.99 | 0.727 | 0.000 |
| StoneMuWall02 | c_wall06 | 10 | 5.95 | 0.733 | 0.000 |
| MerchantAnimal01 | merchant_moster_a02 | 88 | 5.89 | 0.700 | 0.000 |
| TreasureChest01 | treasure_chest | 66 | 5.76 | 0.824 | 0.000 |
| Object27 | flower05 | 384 | 5.72 | 0.900 | 0.000 |
| Waterspout01 | ston01 | 60 | 5.70 | 0.864 | 0.000 |
| HouseWall06 | tile_wood03 | 2 | 5.68 | 0.900 | 0.000 |
| Ship01 | ship06 | 24 | 5.62 | 0.800 | 0.000 |
| Grass05 | tree_09 | 112 | 5.52 | 0.900 | 0.000 |
| Object18 | flower2 | 107 | 5.49 | 0.900 | 0.000 |
| Giant01 | mu2 | 709 | 5.41 | 0.346 | 0.000 |
| Well04 | jar_01 | 388 | 5.25 | 0.305 | 0.000 |
| Object23 | flower2 | 162 | 5.15 | 0.900 | 0.000 |
| Object97 | so_jgtree01 | 308 | 5.14 | 0.879 | 0.000 |
| Furniture05 | desk_big | 34 | 5.13 | 0.722 | 0.000 |
| HouseEtc01 | c_wall04 | 8 | 5.11 | 0.724 | 0.000 |
| Tree11 | tree_03 | 25 | 4.93 | 0.881 | 0.000 |
| Object30 | so_archb02 | 83 | 4.82 | 0.861 | 0.000 |
| Cannon02 | horse_drawn_01 | 148 | 4.68 | 0.764 | 0.568 |
| Object06 | so_grass | 24 | 4.68 | 0.900 | 0.000 |
| SteelDoor01 | steel_barred_door | 2 | 4.58 | 0.722 | 1.000 |
| House04 | tile_wood02 | 16 | 4.48 | 0.813 | 0.000 |
| Wizard01 | mu2 | 495 | 4.46 | 0.780 | 0.000 |
| Object28 | treea02 | 66 | 4.42 | 0.900 | 0.000 |
| Object24 | flower7 | 246 | 4.34 | 0.874 | 0.000 |
| Object23 | flower7 | 246 | 4.34 | 0.874 | 0.000 |
| Object122 | stair_01 | 12 | 4.06 | 0.723 | 0.000 |
| Ship01 | ship05 | 8 | 4.06 | 0.817 | 0.000 |
| Furniture03 | desk_big | 42 | 4.06 | 0.722 | 0.000 |
| StoneStatue03 | angel_stone_statue | 228 | 4.06 | 0.724 | 0.000 |
| Hunter01 | mu2 | 424 | 4.03 | 0.702 | 0.000 |
| HouseWall04 | tile_ston04 | 12 | 3.98 | 0.898 | 0.000 |
| ElfMerchant01 | mu2 | 322 | 3.96 | 0.702 | 0.000 |
| Object22 | flower6 | 39 | 3.87 | 0.900 | 0.000 |
| Object99 | so_jgwall03 | 18 | 3.79 | 0.727 | 0.000 |
| StoneMuWall01 | c_wall06 | 20 | 3.78 | 0.735 | 0.000 |
| MixNpc01 | ob3 | 425 | 3.72 | 0.700 | 0.000 |
| ElfWizard01 | lpwing | 4 | 3.66 | 1.000 | 0.000 |
| Stair01 | tile_wood01 | 78 | 3.58 | 0.807 | 0.000 |
| Object04 | flower02 | 64 | 3.56 | 0.900 | 0.000 |
| Object104 | so_jgwall02 | 164 | 3.50 | 0.728 | 0.000 |
| Well03 | jar_01 | 194 | 3.43 | 0.305 | 0.000 |
| HouseEtc02 | tile_ston06 | 30 | 3.40 | 0.812 | 0.000 |
| Stone02 | ston02 | 34 | 3.39 | 0.900 | 0.000 |
| ElfWizard01 | lptgq | 140 | 3.39 | 0.724 | 0.000 |
| Object50 | gatelight01_r | 96 | 3.18 | 1.000 | 0.000 |
| Object19 | filling_up05 | 72 | 3.12 | 1.000 | 0.000 |
| TreasureDrum01 | drum | 60 | 3.05 | 0.700 | 0.039 |
| ManUpper01 | mu2 | 250 | 3.02 | 0.780 | 0.000 |
| Object24 | flower2 | 162 | 2.97 | 0.900 | 0.000 |
| House01 | tile_house01 | 5 | 2.96 | 0.804 | 0.000 |
| StoneMuWall01 | c_wall05 | 32 | 2.96 | 0.732 | 0.000 |
| HouseEtc02 | tile_house01 | 5 | 2.93 | 0.808 | 0.000 |
| Furniture04 | desk_big | 38 | 2.84 | 0.722 | 0.000 |
| SteelDoor01 | steel_barred_b | 56 | 2.83 | 0.722 | 1.000 |
| Object03 | flower2 | 216 | 2.81 | 0.900 | 0.000 |
| StoneMuWall02 | c_wall05 | 22 | 2.74 | 0.732 | 0.000 |
| Object107 | so_jgwall05 | 64 | 2.74 | 0.859 | 0.000 |
| Object26 | treea03 | 843 | 2.68 | 0.900 | 0.000 |
| Sign02 | notice | 30 | 2.67 | 0.810 | 0.000 |
| HouseWall03 | tile_wood02 | 20 | 2.67 | 0.818 | 0.000 |
| Grass06 | tree_09 | 112 | 2.66 | 0.900 | 0.000 |
| Object40 | yellow_jewel | 147 | 2.64 | 1.000 | 0.000 |
| Object38 | light01 | 6 | 2.64 | 1.000 | 0.000 |
| Cannon03 | horse_drawn_01 | 66 | 2.62 | 0.779 | 0.424 |
| Object20 | wd | 120 | 2.59 | 1.000 | 0.000 |
| Tree07 | tree_03 | 84 | 2.55 | 0.878 | 0.000 |
| Object06 | flower03 | 144 | 2.53 | 0.900 | 0.000 |
| Tomb03 | tombstone | 30 | 2.46 | 0.748 | 0.000 |
| Stone01 | ston02 | 26 | 2.45 | 0.900 | 0.000 |
| ElfWizard01 | hpp | 157 | 2.42 | 0.722 | 1.000 |
| Object04 | flower2 | 80 | 2.39 | 0.900 | 0.000 |
| Tree05 | tree_03 | 85 | 2.34 | 0.882 | 0.000 |
| Bonfire01 | fire_01 | 72 | 2.31 | 0.814 | 0.000 |
| Object41 | ob3 | 376 | 2.30 | 0.700 | 0.000 |
| StoneStatue03 | tombstone_big | 20 | 2.28 | 0.727 | 0.000 |
| House04 | tile_house01 | 2 | 2.25 | 0.807 | 0.000 |
| SteelWall01 | steel_barred_a | 4 | 2.20 | 0.722 | 1.000 |
| SteelWall03 | steel_barred_a | 2 | 2.20 | 0.722 | 1.000 |
| Object05 | flower03 | 192 | 2.20 | 0.900 | 0.000 |
| Hound01 | hidden | 172 | 2.16 | 0.445 | 1.000 |
| Object03 | flower02 | 48 | 2.16 | 0.900 | 0.000 |
| Object40 | ob3 | 376 | 2.16 | 0.700 | 0.000 |
| Goblin01 | mu2 | 376 | 2.09 | 0.697 | 0.000 |
| StreetLight01 | streetlight | 48 | 1.92 | 0.838 | 0.000 |
| ChainScorpion01 | mu2 | 252 | 1.87 | 0.702 | 0.000 |
| Furniture02 | bottle | 432 | 1.86 | 0.722 | 0.000 |
| HouseWall02 | light_02 | 6 | 1.81 | 1.000 | 0.000 |
| House03 | light_02 | 6 | 1.81 | 1.000 | 0.000 |
| Curtain01 | badge_02 | 8 | 1.78 | 0.729 | 0.000 |
| Object65 | statuedsw052 | 276 | 1.76 | 0.740 | 0.000 |
| BridgeStone01 | tile_wood02 | 8 | 1.76 | 0.817 | 0.000 |
| HouseEtc01 | c_wall06 | 26 | 1.76 | 0.728 | 0.000 |
| Skeleton01 | mu2 | 486 | 1.72 | 0.584 | 0.000 |
| ArmorMale10 | mu2 | 198 | 1.61 | 0.346 | 1.000 |
| Fence01 | tile_wood02 | 36 | 1.58 | 0.821 | 0.000 |
| Shield13 | mu2 | 114 | 1.58 | 0.469 | 1.000 |
| Object18 | flower9 | 42 | 1.54 | 1.000 | 0.000 |
| HouseWall01 | tile_ston04 | 8 | 1.53 | 0.894 | 0.000 |
| HouseWall02 | tile_ston04 | 8 | 1.53 | 0.898 | 0.000 |
| Object130 | angeflo_r | 12 | 1.50 | 1.000 | 0.000 |
| Object80 | br001 | 12 | 1.50 | 0.722 | 0.000 |
| Stone05 | ston01 | 120 | 1.47 | 0.879 | 0.000 |
| SteelWall02 | steel_barred_b | 64 | 1.40 | 0.722 | 1.000 |
| Furniture06 | bookshelf | 52 | 1.40 | 0.722 | 0.000 |
| Bonfire01 | fire_02 | 38 | 1.35 | 1.000 | 0.000 |
| ArmorMale05 | mu2 | 173 | 1.29 | 0.584 | 0.000 |
| Object07 | filling_up05 | 16 | 1.27 | 0.900 | 0.000 |
| Fence03 | joint | 10 | 1.27 | 0.801 | 0.000 |
| PantMale08 | mu2 | 140 | 1.24 | 0.683 | 1.000 |
| Object34 | ston001 | 20 | 1.20 | 0.867 | 0.000 |
| BootMale10 | mu2 | 186 | 1.14 | 0.345 | 1.000 |
| Fence02 | joint | 16 | 1.13 | 0.794 | 0.000 |
| HouseWall02 | tile_ston06 | 10 | 1.13 | 0.812 | 0.000 |
| Object22 | flower4 | 12 | 1.06 | 0.796 | 0.000 |
| PantMale05 | mu2 | 119 | 1.06 | 0.585 | 0.000 |
| Sign01 | notice | 24 | 1.06 | 0.801 | 0.000 |
| PantMale01 | mu2 | 118 | 1.06 | 0.465 | 1.000 |
| PantClass01 | mu2 | 120 | 1.04 | 0.720 | 0.000 |
| House04 | tile_windows01 | 4 | 1.03 | 0.831 | 0.000 |
| BootMale01 | mu2 | 170 | 1.03 | 0.464 | 1.000 |
| ArmorMale07 | mu2 | 170 | 1.02 | 0.555 | 1.000 |
| FemaleLower01 | mu2 | 88 | 0.99 | 0.780 | 0.000 |
| Curtain01 | badge_01 | 60 | 0.98 | 0.726 | 0.000 |
| ElfWizard01 | lplower | 328 | 0.98 | 0.700 | 0.000 |
| Object18 | flower3 | 117 | 0.95 | 0.900 | 0.000 |
| Shield08 | mu2 | 56 | 0.93 | 0.685 | 1.000 |
| FemaleUpper01 | mu2 | 254 | 0.93 | 0.780 | 0.000 |
| Shield07 | mu2 | 108 | 0.93 | 0.584 | 0.000 |
| GirlLower01 | mu2 | 176 | 0.92 | 0.779 | 0.000 |
| BootMale07 | mu2 | 148 | 0.91 | 0.553 | 1.000 |
| Shield12 | mu2 | 72 | 0.90 | 0.472 | 1.000 |
| PantClass02 | mu2 | 612 | 0.89 | 0.702 | 0.000 |
| BeetleMonster01 | bug2 | 4 | 0.88 | 1.000 | 0.000 |
| BootMale03 | mu2 | 132 | 0.87 | 0.446 | 1.000 |
| ManBoots01 | mu2 | 108 | 0.87 | 0.780 | 0.000 |
| PantMale06 | mu2 | 122 | 0.86 | 0.753 | 0.000 |
| Object08 | flower3 | 39 | 0.86 | 0.900 | 0.000 |
| PantMale07 | mu2 | 110 | 0.83 | 0.554 | 1.000 |
| PantMale10 | mu2 | 94 | 0.82 | 0.346 | 1.000 |
| Object01 | flower03 | 80 | 0.81 | 0.900 | 0.000 |
| Shield03 | mu2 | 24 | 0.80 | 0.735 | 0.000 |
| Object01 | flower2 | 54 | 0.79 | 0.900 | 0.000 |
| HouseEtc03 | steel_barred_door | 4 | 0.78 | 0.400 | 0.580 |
| Shield06 | mu2 | 20 | 0.78 | 0.740 | 0.000 |
| PantClass03 | mu2 | 110 | 0.78 | 0.780 | 0.000 |
| BootClass01 | mu2 | 126 | 0.78 | 0.625 | 0.000 |
| SteelWall01 | steel_barred_b | 48 | 0.78 | 0.722 | 1.000 |
| BootClass02 | mu2 | 756 | 0.74 | 0.621 | 0.000 |
| Object35 | flower03 | 40 | 0.73 | 0.900 | 0.000 |
| StoneMuWall01 | bridge_01 | 128 | 0.73 | 0.725 | 0.000 |
| StoneWall01 | bridge_01 | 128 | 0.73 | 0.725 | 0.000 |
| StoneWall02 | bridge_01 | 128 | 0.73 | 0.725 | 0.000 |
| Carriage01 | horse_drawn_03 | 2 | 0.73 | 1.000 | 0.000 |
| Stone04 | ston01 | 45 | 0.72 | 0.879 | 0.000 |
| Object11 | flower5 | 30 | 0.72 | 0.808 | 0.000 |
| Shield05 | mu2 | 22 | 0.71 | 0.722 | 1.000 |
| BootMale08 | mu2 | 120 | 0.70 | 0.683 | 1.000 |
| Shield02 | mu2 | 26 | 0.69 | 0.722 | 1.000 |
| BootMale05 | mu2 | 124 | 0.69 | 0.584 | 0.000 |
| Shield11 | mu2 | 34 | 0.68 | 0.722 | 1.000 |
| Fence04 | joint | 10 | 0.67 | 0.800 | 0.000 |
| Object43 | itp02 | 30 | 0.67 | 0.722 | 1.000 |
| BootMale06 | mu2 | 124 | 0.66 | 0.750 | 0.000 |
| Shield09 | mu2 | 22 | 0.66 | 0.744 | 0.000 |
| FireLight01 | light | 20 | 0.65 | 0.722 | 1.000 |
| FireLight01 | light2 | 22 | 0.63 | 1.000 | 0.000 |
| ArmorClass02 | mu2 | 120 | 0.62 | 0.702 | 0.000 |
| Object09 | flower4 | 30 | 0.61 | 0.799 | 0.000 |
| Beer01 | plate2 | 110 | 0.60 | 0.722 | 0.000 |
| Object18 | flower03 | 80 | 0.59 | 0.900 | 0.000 |
| Tree07 | tree_04 | 6 | 0.59 | 0.819 | 0.000 |
| ManGloves01 | mu2 | 132 | 0.58 | 0.780 | 0.000 |
| Furniture07 | bookshelf | 42 | 0.58 | 0.722 | 0.000 |
| GloveMale10 | mu2 | 174 | 0.58 | 0.348 | 0.987 |
| BootClass03 | mu2 | 106 | 0.56 | 0.741 | 0.000 |
| BridgeStone01 | tree_04 | 12 | 0.55 | 0.820 | 0.000 |
| ArmorClass03 | mu2 | 120 | 0.55 | 0.780 | 0.000 |
| GloveMale01 | mu2 | 194 | 0.54 | 0.464 | 1.000 |
| Furniture01 | pot2 | 54 | 0.54 | 0.722 | 0.000 |
| Shield10 | mu2 | 20 | 0.54 | 0.342 | 1.000 |
| Furniture01 | bottle | 108 | 0.53 | 0.722 | 0.000 |
| Book01 | mu2 | 16 | 0.53 | 0.741 | 0.000 |
| Object10 | filling_up04 | 18 | 0.53 | 0.722 | 1.000 |
| FemaleBoots01 | mu2 | 130 | 0.52 | 0.779 | 0.000 |
| GirlUpper01 | mu2 | 228 | 0.51 | 0.780 | 0.000 |
| StoneWall06 | badge_03 | 8 | 0.48 | 0.800 | 0.000 |
| StoneWall04 | badge_03 | 8 | 0.48 | 0.800 | 0.000 |
| Shield01 | mu2 | 18 | 0.47 | 0.721 | 0.000 |
| GloveMale07 | mu2 | 172 | 0.47 | 0.554 | 0.999 |
| GloveMale05 | mu2 | 156 | 0.46 | 0.584 | 0.000 |
| Grass07 | mushroom | 126 | 0.43 | 0.900 | 0.000 |
| Object37 | flower2 | 100 | 0.43 | 0.900 | 0.000 |
| Bird01 | bird | 98 | 0.43 | 0.900 | 0.000 |
| Spear03 | mu2 | 46 | 0.42 | 0.722 | 1.000 |
| Cannon01 | horse_drawn_01__wrought_iron | 6 | 0.40 | 0.722 | 1.000 |
| GloveMale03 | mu2 | 152 | 0.37 | 0.444 | 0.997 |
| HelmMale01 | mu2 | 68 | 0.37 | 0.465 | 1.000 |
| Beer03 | bottle | 72 | 0.36 | 0.722 | 0.000 |
| StoneWall06 | badge_01 | 28 | 0.36 | 0.784 | 0.000 |
| StoneWall04 | badge_01 | 28 | 0.36 | 0.784 | 0.000 |
| Object12 | flower | 88 | 0.36 | 0.900 | 0.000 |
| SteelWall03 | steel_barred_b | 16 | 0.35 | 0.722 | 1.000 |
| GloveMale06 | mu2 | 148 | 0.34 | 0.748 | 0.000 |
| Candle01 | candle | 110 | 0.33 | 0.722 | 0.000 |
| Object19 | metal01 | 18 | 0.33 | 0.722 | 1.000 |
| Staff01 | mu2 | 150 | 0.33 | 0.722 | 1.000 |
| Object10 | filling_up03 | 10 | 0.33 | 0.826 | 0.000 |
| GloveMale08 | mu2 | 148 | 0.32 | 0.683 | 1.000 |
| Object50 | gatewall02 | 120 | 0.31 | 0.730 | 0.000 |
| Staff02 | mu2 | 50 | 0.31 | 0.663 | 0.000 |
| GloveClass02 | mu2 | 780 | 0.30 | 0.617 | 0.000 |
| Object13 | flower | 72 | 0.29 | 0.900 | 0.000 |
| Object14 | flower | 72 | 0.29 | 0.900 | 0.000 |
| Object17 | flower | 72 | 0.29 | 0.900 | 0.000 |
| HelmMale03 | mu2 | 92 | 0.29 | 0.721 | 0.000 |
| Object10 | wood02 | 80 | 0.29 | 1.000 | 0.000 |
| GloveClass01 | mu2 | 130 | 0.29 | 0.619 | 0.000 |
| GloveElf01 | mu2 | 152 | 0.29 | 0.736 | 0.000 |
| Sign01 | signboard | 2 | 0.29 | 0.722 | 1.000 |
| Staff03 | mu2 | 68 | 0.28 | 0.722 | 1.000 |
| Object02 | flower2 | 45 | 0.25 | 0.900 | 0.000 |
| Beer01 | winecup | 36 | 0.25 | 0.722 | 0.000 |
| Beer03 | winecup | 36 | 0.25 | 0.722 | 0.000 |
| Beer02 | winecup | 36 | 0.25 | 0.722 | 0.000 |
| Beer02 | plate | 60 | 0.25 | 0.722 | 0.000 |
| FemaleHead01 | mu2 | 104 | 0.25 | 0.780 | 0.000 |
| ManHead01 | mu2 | 94 | 0.24 | 0.780 | 0.000 |
| GloveClass03 | mu2 | 130 | 0.23 | 0.780 | 0.000 |
| Object16 | flower | 56 | 0.23 | 0.900 | 0.000 |
| FireLight02 | tile_02 | 24 | 0.22 | 0.815 | 0.000 |
| Gem05 | mu2 | 48 | 0.22 | 0.722 | 0.000 |
| Object18 | wood02 | 32 | 0.21 | 1.000 | 0.000 |
| House04 | bridge_01 | 12 | 0.21 | 0.722 | 0.000 |
| Carriage01 | horse_drawn_02 | 8 | 0.21 | 0.722 | 0.000 |
| Object18 | flower02 | 32 | 0.20 | 0.900 | 0.000 |
| BullFighter01 | hidden | 26 | 0.20 | 0.620 | 0.000 |
| Beer01 | bottle | 36 | 0.20 | 0.722 | 0.000 |
| Ale01 | mu2 | 36 | 0.20 | 0.185 | 0.000 |
| Beer02 | bottle | 36 | 0.20 | 0.722 | 0.000 |
| Furniture01 | pot | 78 | 0.20 | 0.722 | 0.000 |
| Object36 | flower10 | 36 | 0.20 | 1.000 | 0.000 |
| GirlHead01 | mu2 | 92 | 0.19 | 0.780 | 0.000 |
| HelmElf01 | mu2 | 58 | 0.19 | 0.739 | 0.000 |
| House04 | tile_space01 | 12 | 0.19 | 1.000 | 0.000 |
| Beer02 | apple | 96 | 0.18 | 0.722 | 0.000 |
| Object35 | flower2 | 9 | 0.17 | 0.900 | 0.000 |
| StreetLight01 | streetlight_brightness2 | 36 | 0.17 | 1.000 | 0.000 |
| Object07 | flower03 | 20 | 0.17 | 0.900 | 0.000 |
| Object15 | flower | 40 | 0.16 | 0.900 | 0.000 |
| Object11 | flower03 | 16 | 0.15 | 0.900 | 0.000 |
| Beer01 | plate | 30 | 0.15 | 0.722 | 0.000 |
| Object36 | flower2 | 39 | 0.14 | 0.900 | 0.000 |
| Grass08 | mushroom | 54 | 0.14 | 0.900 | 0.000 |
| MixNpc01 | yellow_jewel | 32 | 0.12 | 1.000 | 0.000 |
| Jewel15 | mu2 | 32 | 0.12 | 0.180 | 0.000 |
| FireLight02 | fire_light_01 | 8 | 0.12 | 1.000 | 0.000 |
| Object65 | statuedeye_r | 69 | 0.12 | 1.000 | 0.000 |
| Jewel02 | mu2 | 22 | 0.11 | 0.180 | 0.000 |
| Potion05 | mu2 | 28 | 0.11 | 0.722 | 0.000 |
| Potion02 | mu2 | 28 | 0.11 | 0.722 | 0.000 |
| FireLight01 | light3 | 6 | 0.10 | 1.000 | 0.000 |
| Furniture06 | chair2 | 2 | 0.09 | 0.722 | 0.000 |
| Jewel01 | mu2 | 12 | 0.09 | 0.180 | 0.000 |
| NewFace02 | mu2 | 1502 | 0.08 | 0.701 | 0.000 |
| Cannon03 | horse_drawn_01__wrought_iron | 9 | 0.07 | 0.722 | 1.000 |
| Butterfly01 | Butterfly | 28 | 0.07 | 0.900 | 0.000 |
| NewFace01 | mu2 | 1208 | 0.07 | 0.702 | 0.000 |
| CrossBow04 | nocked | 10 | 0.05 | 0.504 | 1.000 |
| CrossBow03 | nocked | 10 | 0.05 | 0.507 | 1.000 |
| Bow02 | nocked | 10 | 0.05 | 0.725 | 0.000 |
| Gold01 | mu2 | 32 | 0.04 | 0.722 | 1.000 |
| Candle01 | candle2 | 6 | 0.04 | 1.000 | 0.000 |
| Object02 | filling_up02 | 24 | 0.03 | 1.000 | 0.000 |
| Sign01 | doorknob | 4 | 0.03 | 0.722 | 1.000 |
| Object10 | filling_up02 | 8 | 0.03 | 1.000 | 0.000 |
| NewFace03 | mu2 | 1579 | 0.03 | 0.701 | 0.000 |
| Cannon02 | horse_drawn_01__wrought_iron | 7 | 0.02 | 0.722 | 1.000 |
| FireLight02 | copra_gate | 2 | 0.02 | 0.723 | 0.000 |
| Beer01 | pot3 | 4 | 0.01 | 0.900 | 0.000 |
| Beer02 | pot3 | 4 | 0.01 | 0.900 | 0.000 |

## Every slot

| model | slot | claims | ORM rough | asks | ORM metal | asks | flags |
|---|---|---|---:|---:|---:|---:|---|
| Agon01 | mu2 | **none** | - | - | - | - | two-sided |
| Agon01 | brass | brass | 0.446 | 0.44 | 1.000 | 1.00 | two-sided |
| Agon01 | fur | fur | 0.780 | 0.78 | 0.000 | 0.00 | two-sided |
| Ale01 | mu2 | **none** | 0.185 | - | 0.000 | - | cutout 0.50, two-sided |
| Antidote01 | mu2 | **none** | - | - | - | - | two-sided |
| Antidote01 | brass | brass | 0.722 | 0.44 | 1.000 | 1.00 | two-sided |
| Antidote01 | glass | glass | 0.722 | 0.12 | 0.004 | 0.00 | two-sided |
| ArmorClass01 | mu2 | **none** | - | - | - | - | two-sided |
| ArmorClass01 | cloth | cloth | 0.710 | 0.72 | 0.000 | 0.00 | two-sided |
| ArmorClass01 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | two-sided |
| ArmorClass02 | mu2 | **none** | 0.702 | - | 0.000 | - | two-sided |
| ArmorClass03 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| ArmorElf01 | mu2 | **none** | - | - | - | - | two-sided |
| ArmorElf01 | leather | leather | 0.735 | 0.62 | 0.000 | 0.00 | two-sided |
| ArmorElf01 | skin | skin | 0.780 | 0.70 | 0.000 | 0.00 | two-sided |
| ArmorMale01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| ArmorMale01 | plate_steel | plate_steel | 0.463 | 0.34 | 1.000 | 1.00 | cutout 0.50, two-sided |
| ArmorMale01 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | cutout 0.50, two-sided |
| ArmorMale03 | mu2 | **none** | - | - | - | - | two-sided |
| ArmorMale03 | cloth | cloth | 0.722 | 0.72 | 0.000 | 0.00 | two-sided |
| ArmorMale03 | feather | feather | 0.665 | 0.66 | 0.000 | 0.00 | two-sided |
| ArmorMale03 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | two-sided |
| ArmorMale05 | mu2 | **none** | 0.584 | - | 0.000 | - | two-sided |
| ArmorMale06 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| ArmorMale06 | leather | leather | 0.752 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| ArmorMale06 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | cutout 0.50, two-sided |
| ArmorMale07 | mu2 | **none** | 0.555 | - | 1.000 | - | two-sided |
| ArmorMale08 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| ArmorMale08 | cloth | cloth | 0.722 | 0.72 | 0.000 | 0.00 | cutout 0.50, two-sided |
| ArmorMale08 | plate_steel | plate_steel | 0.681 | 0.34 | 0.986 | 1.00 | cutout 0.50, two-sided |
| ArmorMale09 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| ArmorMale09 | brass | brass | 0.449 | 0.44 | 1.000 | 1.00 | cutout 0.50, two-sided |
| ArmorMale09 | plate_steel | plate_steel | 0.346 | 0.34 | 1.000 | 1.00 | cutout 0.50, two-sided |
| ArmorMale10 | mu2 | **none** | 0.346 | - | 1.000 | - | two-sided |
| Arrows01 | mu2 | **none** | - | - | - | - | two-sided |
| Arrows01 | steel | steel | 0.323 | 0.32 | 1.000 | 1.00 | two-sided |
| Arrows01 | wood | wood | 0.726 | 0.72 | 0.000 | 0.00 | two-sided |
| Arrows02 | mu2 | **none** | - | - | - | - | two-sided |
| Arrows02 | cloth | cloth | 0.800 | 0.72 | 0.000 | 0.00 | two-sided |
| Arrows02 | leather | leather | 0.741 | 0.62 | 0.000 | 0.00 | two-sided |
| Arrows02 | wood | wood | 0.723 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe01 | mu2 | **none** | - | - | - | - | two-sided |
| Axe01 | steel | steel | 0.319 | 0.32 | 1.000 | 1.00 | two-sided |
| Axe01 | wood | wood | 0.716 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe02 | mu2 | **none** | - | - | - | - | two-sided |
| Axe02 | steel | steel | 0.722 | 0.32 | 1.000 | 1.00 | two-sided |
| Axe02 | wood | wood | 0.729 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe03 | mu2 | **none** | - | - | - | - | two-sided |
| Axe03 | brass | brass | 0.459 | 0.44 | 1.000 | 1.00 | two-sided |
| Axe03 | plate_steel | plate_steel | 0.692 | 0.34 | 1.000 | 1.00 | two-sided |
| Axe03 | steel | steel | 0.321 | 0.32 | 1.000 | 1.00 | two-sided |
| Axe04 | mu2 | **none** | - | - | - | - | two-sided |
| Axe04 | leather_wrap | leather_wrap | 0.750 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe04 | steel | steel | 0.323 | 0.32 | 1.000 | 1.00 | two-sided |
| Axe04 | wood | wood | 0.726 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe06 | mu2 | **none** | - | - | - | - | two-sided |
| Axe06 | plate_steel | plate_steel | 0.724 | 0.34 | 1.000 | 1.00 | two-sided |
| Axe06 | steel | steel | 0.722 | 0.32 | 1.000 | 1.00 | two-sided |
| Axe06 | wood | wood | 0.727 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe07 | mu2 | **none** | - | - | - | - | two-sided |
| Axe07 | leather_wrap | leather_wrap | 0.735 | 0.72 | 0.000 | 0.00 | two-sided |
| Axe07 | steel | steel | 0.722 | 0.32 | 1.000 | 1.00 | two-sided |
| Axe07 | wood | wood | 0.726 | 0.72 | 0.000 | 0.00 | two-sided |
| Beer01 | plate | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer01 | winecup | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer01 | plate2 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer01 | pot3 | **none** | 0.900 | - | 0.000 | - | cutout 0.25, two-sided |
| Beer01 | bottle | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer02 | pot3 | **none** | 0.900 | - | 0.000 | - | cutout 0.25, two-sided |
| Beer02 | apple | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer02 | bottle | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer02 | plate | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer02 | winecup | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer03 | bottle | **none** | 0.722 | - | 0.000 | - | two-sided |
| Beer03 | winecup | **none** | 0.722 | - | 0.000 | - | two-sided |
| BeetleMonster01 | bug | **none** | 0.620 | - | 0.000 | - | two-sided |
| BeetleMonster01 | bug2 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Bird01 | bird | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Bonfire01 | fire_01 | **none** | 0.814 | - | 0.000 | - | two-sided |
| Bonfire01 | fire_02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Book01 | mu2 | **none** | 0.741 | - | 0.000 | - | two-sided |
| BootClass01 | mu2 | **none** | 0.625 | - | 0.000 | - | two-sided |
| BootClass02 | mu2 | **none** | 0.621 | - | 0.000 | - | two-sided |
| BootClass03 | mu2 | **none** | 0.741 | - | 0.000 | - | two-sided |
| BootElf01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| BootElf01 | brass | brass | 0.476 | 0.44 | 1.000 | 1.00 | cutout 0.50, two-sided |
| BootElf01 | leather | leather | 0.743 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| BootMale01 | mu2 | **none** | 0.464 | - | 1.000 | - | two-sided |
| BootMale03 | mu2 | **none** | 0.446 | - | 1.000 | - | cutout 0.50, two-sided |
| BootMale05 | mu2 | **none** | 0.584 | - | 0.000 | - | two-sided |
| BootMale06 | mu2 | **none** | 0.750 | - | 0.000 | - | two-sided |
| BootMale07 | mu2 | **none** | 0.553 | - | 1.000 | - | two-sided |
| BootMale08 | mu2 | **none** | 0.683 | - | 1.000 | - | two-sided |
| BootMale09 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| BootMale09 | brass | brass | 0.448 | 0.44 | 1.000 | 1.00 | cutout 0.50, two-sided |
| BootMale09 | plate_steel | plate_steel | 0.345 | 0.34 | 1.000 | 1.00 | cutout 0.50, two-sided |
| BootMale10 | mu2 | **none** | 0.345 | - | 1.000 | - | two-sided |
| Bow01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| Bow01 | cloth | cloth | 0.800 | 0.72 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Bow01 | wood | wood | 0.725 | 0.72 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Bow02 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| Bow02 | cloth | cloth | 0.800 | 0.72 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Bow02 | nocked | **none** | 0.725 | - | 0.000 | - | cutout 0.50, two-sided |
| Bow02 | wood | wood | 0.726 | 0.72 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Bridge01 | bridge_01 | **none** | 0.726 | - | 0.000 | - | two-sided |
| Bridge01 | bridge_shadow01 | **none** | - | - | - | - | two-sided |
| BridgeStone01 | bridge_shadow01 | **none** | - | - | - | - | two-sided |
| BridgeStone01 | tile_ston06 | **none** | 0.822 | - | 0.000 | - | two-sided |
| BridgeStone01 | tile_wood02 | **none** | 0.817 | - | 0.000 | - | two-sided |
| BridgeStone01 | tree_04 | **none** | 0.820 | - | 0.000 | - | two-sided |
| BudgeDragon01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| BudgeDragon01 | chitin | chitin | 0.620 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| BudgeDragon01 | leather | leather | 0.612 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| BullFighter01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| BullFighter01 | chitin | chitin | 0.620 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| BullFighter01 | fur | fur | 0.778 | 0.78 | 0.000 | 0.00 | cutout 0.50, two-sided |
| BullFighter01 | hidden | **none** | 0.620 | - | 0.000 | - | cutout 0.50, two-sided |
| BullFighter01 | leather | leather | 0.615 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Butterfly01 | Butterfly | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Candle01 | candle | **none** | 0.722 | - | 0.000 | - | two-sided |
| Candle01 | candle2 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Cannon01 | horse_drawn_01 | **none** | 0.770 | - | 0.491 | - | two-sided |
| Cannon01 | horse_drawn_01__wrought_iron | **none** | 0.722 | - | 1.000 | - | two-sided |
| Cannon02 | horse_drawn_01 | **none** | 0.764 | - | 0.568 | - | two-sided |
| Cannon02 | horse_drawn_01__wrought_iron | **none** | 0.722 | - | 1.000 | - | two-sided |
| Cannon03 | horse_drawn_01 | **none** | 0.779 | - | 0.424 | - | two-sided |
| Cannon03 | horse_drawn_01__wrought_iron | **none** | 0.722 | - | 1.000 | - | two-sided |
| Carriage01 | horse_drawn_01 | **none** | 0.807 | - | 0.000 | - | two-sided |
| Carriage01 | horse_drawn_02 | **none** | 0.722 | - | 0.000 | - | cutout 0.50, two-sided |
| Carriage01 | horse_drawn_03 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Carriage02 | horse_drawn_01 | **none** | 0.811 | - | 0.000 | - | two-sided |
| Carriage04 | horse_drawn_01 | **none** | 0.811 | - | 0.000 | - | two-sided |
| Carriage04 | grass_01 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| ChainScorpion01 | mu2 | **none** | 0.702 | - | 0.000 | - | two-sided |
| CrossBow01 | mu2 | **none** | - | - | - | - | two-sided |
| CrossBow01 | steel | steel | 0.325 | 0.32 | 1.000 | 1.00 | two-sided |
| CrossBow01 | wood | wood | 0.723 | 0.72 | 0.000 | 0.00 | two-sided |
| CrossBow01 | wrought_iron | wrought_iron | 0.498 | 0.50 | 1.000 | 1.00 | two-sided |
| CrossBow03 | mu2 | **none** | - | - | - | - | two-sided |
| CrossBow03 | leather_wrap | leather_wrap | 0.750 | 0.72 | 0.000 | 0.00 | two-sided |
| CrossBow03 | nocked | **none** | 0.507 | - | 1.000 | - | two-sided |
| CrossBow03 | steel | steel | 0.325 | 0.32 | 1.000 | 1.00 | two-sided |
| CrossBow03 | wood | wood | 0.724 | 0.72 | 0.000 | 0.00 | two-sided |
| CrossBow04 | mu2 | **none** | - | - | - | - | two-sided |
| CrossBow04 | leather_wrap | leather_wrap | 0.733 | 0.72 | 0.000 | 0.00 | two-sided |
| CrossBow04 | nocked | **none** | 0.504 | - | 1.000 | - | two-sided |
| CrossBow04 | wood | wood | 0.724 | 0.72 | 0.000 | 0.00 | two-sided |
| Curtain01 | badge_01 | **none** | 0.726 | - | 0.000 | - | two-sided |
| Curtain01 | badge_02 | **none** | 0.729 | - | 0.000 | - | cutout 0.50, two-sided |
| DoungeonGate01 | copra_gate | **none** | 0.736 | - | 0.000 | - | two-sided |
| ElfMerchant01 | mu2 | **none** | 0.702 | - | 0.000 | - | two-sided |
| ElfWizard01 | hpp | **none** | 0.722 | - | 1.000 | - | two-sided |
| ElfWizard01 | lpwing | **none** | 1.000 | - | 0.000 | - | two-sided |
| ElfWizard01 | lplower | **none** | 0.700 | - | 0.000 | - | two-sided |
| ElfWizard01 | lptgq | **none** | 0.724 | - | 0.000 | - | cutout 0.25, two-sided |
| FemaleBoots01 | mu2 | **none** | 0.779 | - | 0.000 | - | two-sided |
| FemaleHead01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| FemaleLower01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| FemaleUpper01 | mu2 | **none** | 0.780 | - | 0.000 | - | cutout 0.50, two-sided |
| Fence01 | tile_wood02 | **none** | 0.821 | - | 0.000 | - | two-sided |
| Fence02 | joint | **none** | 0.794 | - | 0.000 | - | two-sided |
| Fence03 | joint | **none** | 0.801 | - | 0.000 | - | two-sided |
| Fence04 | joint | **none** | 0.800 | - | 0.000 | - | two-sided |
| FireLight01 | light | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| FireLight01 | light2 | **none** | 1.000 | - | 0.000 | - | two-sided |
| FireLight01 | light3 | **none** | 1.000 | - | 0.000 | - | two-sided |
| FireLight02 | tile_02 | **none** | 0.815 | - | 0.000 | - | two-sided |
| FireLight02 | fire_light_01 | **none** | 1.000 | - | 0.000 | - | two-sided |
| FireLight02 | copra_gate | **none** | 0.723 | - | 0.000 | - | two-sided |
| ForestMonster01 | mu2 | **none** | 0.620 | - | 0.000 | - | two-sided |
| Furniture01 | pot | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture01 | pot2 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture01 | bookshelf | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture01 | bottle | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture02 | bottle | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture02 | bookshelf | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture03 | desk_big | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture04 | desk_big | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture05 | desk_big | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture06 | bookshelf | **none** | 0.722 | - | 0.000 | - | two-sided |
| Furniture06 | chair2 | **none** | 0.722 | - | 0.000 | - | cutout 0.25, two-sided |
| Furniture07 | bookshelf | **none** | 0.722 | - | 0.000 | - | two-sided |
| Gem05 | mu2 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Giant01 | mu2 | **none** | 0.346 | - | 0.000 | - | two-sided |
| GirlHead01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| GirlLower01 | mu2 | **none** | 0.779 | - | 0.000 | - | two-sided |
| GirlUpper01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| GloveClass01 | mu2 | **none** | 0.619 | - | 0.000 | - | two-sided |
| GloveClass02 | mu2 | **none** | 0.617 | - | 0.000 | - | two-sided |
| GloveClass03 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| GloveElf01 | mu2 | **none** | 0.736 | - | 0.000 | - | two-sided |
| GloveMale01 | mu2 | **none** | 0.464 | - | 1.000 | - | two-sided |
| GloveMale03 | mu2 | **none** | 0.444 | - | 0.997 | - | two-sided |
| GloveMale05 | mu2 | **none** | 0.584 | - | 0.000 | - | two-sided |
| GloveMale06 | mu2 | **none** | 0.748 | - | 0.000 | - | two-sided |
| GloveMale07 | mu2 | **none** | 0.554 | - | 0.999 | - | two-sided |
| GloveMale08 | mu2 | **none** | 0.683 | - | 1.000 | - | two-sided |
| GloveMale09 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| GloveMale09 | brass | brass | 0.446 | 0.44 | 1.000 | 1.00 | cutout 0.50, two-sided |
| GloveMale09 | plate_steel | plate_steel | 0.347 | 0.34 | 0.998 | 1.00 | cutout 0.50, two-sided |
| GloveMale10 | mu2 | **none** | 0.348 | - | 0.987 | - | two-sided |
| Goblin01 | mu2 | **none** | 0.697 | - | 0.000 | - | two-sided |
| Gold01 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Grass01 | tree_08 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass02 | tree_08 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass03 | tree_01 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass03 | tree_02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass04 | tree_01 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass04 | tree_02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass05 | tree_09 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass06 | tree_09 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass07 | mushroom | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Grass08 | mushroom | **none** | 0.900 | - | 0.000 | - | two-sided |
| Hanging01 | horse_drawn_01 | **none** | 0.814 | - | 0.000 | - | two-sided |
| HelmClass01 | mu2 | **none** | - | - | - | - | two-sided |
| HelmClass01 | hair | hair | 0.866 | 0.85 | 0.000 | 0.00 | two-sided |
| HelmClass01 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | two-sided |
| HelmClass02 | mu2 | **none** | - | - | - | - | two-sided |
| HelmClass02 | hair | hair | 0.870 | 0.85 | 0.000 | 0.00 | two-sided |
| HelmClass02 | skin | skin | 0.701 | 0.70 | 0.000 | 0.00 | two-sided |
| HelmClass03 | mu2 | **none** | - | - | - | - | two-sided |
| HelmClass03 | hair | hair | 0.722 | 0.85 | 0.000 | 0.00 | two-sided |
| HelmClass03 | skin | skin | 0.780 | 0.70 | 0.000 | 0.00 | two-sided |
| HelmElf01 | mu2 | **none** | 0.739 | - | 0.000 | - | two-sided |
| HelmMale01 | mu2 | **none** | 0.465 | - | 1.000 | - | two-sided |
| HelmMale03 | mu2 | **none** | 0.721 | - | 0.000 | - | two-sided |
| HelmMale05 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| HelmMale05 | bone | bone | 0.606 | 0.58 | 0.000 | 0.00 | cutout 0.50, two-sided |
| HelmMale05 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | cutout 0.50, two-sided |
| HelmMale06 | mu2 | **none** | - | - | - | - | two-sided |
| HelmMale06 | leather | leather | 0.749 | 0.62 | 0.000 | 0.00 | two-sided |
| HelmMale06 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | two-sided |
| HelmMale07 | mu2 | **none** | - | - | - | - | two-sided |
| HelmMale07 | plate_steel | plate_steel | 0.553 | 0.34 | 1.000 | 1.00 | two-sided |
| HelmMale07 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | two-sided |
| HelmMale08 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| HelmMale08 | plate_steel | plate_steel | 0.683 | 0.34 | 1.000 | 1.00 | cutout 0.50, two-sided |
| HelmMale08 | skin | skin | 0.696 | 0.70 | 0.000 | 0.00 | cutout 0.50, two-sided |
| HelmMale09 | mu2 | **none** | - | - | - | - | two-sided |
| HelmMale09 | plate_steel | plate_steel | 0.346 | 0.34 | 1.000 | 1.00 | two-sided |
| HelmMale09 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | two-sided |
| HelmMale10 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| HelmMale10 | feather | feather | 0.662 | 0.66 | 0.000 | 0.00 | cutout 0.50, two-sided |
| HelmMale10 | plate_steel | plate_steel | 0.346 | 0.34 | 1.000 | 1.00 | cutout 0.50, two-sided |
| Hound01 | mu2 | **none** | - | - | - | - | two-sided |
| Hound01 | fur | fur | 0.780 | 0.78 | 0.000 | 0.00 | two-sided |
| Hound01 | hidden | **none** | 0.445 | - | 1.000 | - | two-sided |
| Hound01 | painted_steel | painted_steel | 0.498 | 0.28 | 0.000 | 0.00 | two-sided |
| House01 | tile_house01 | **none** | 0.804 | - | 0.000 | - | two-sided |
| House01 | tile_ston04 | **none** | 0.898 | - | 0.000 | - | two-sided |
| House01 | tile_ston05 | **none** | 0.910 | - | 0.000 | - | two-sided |
| House01 | tile_ston06 | **none** | 0.816 | - | 0.000 | - | two-sided |
| House02 | drum | **none** | 0.819 | - | 0.000 | - | two-sided |
| House02 | steel | steel | 0.722 | 0.32 | 1.000 | 1.00 | two-sided |
| House03 | tile_ston05 | **none** | 0.894 | - | 0.000 | - | two-sided |
| House03 | tile_ston04 | **none** | 0.902 | - | 0.000 | - | two-sided |
| House03 | tile_ston06 | **none** | 0.818 | - | 0.000 | - | two-sided |
| House03 | tile_ston07 | **none** | 0.822 | - | 0.000 | - | two-sided |
| House03 | light_02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| House04 | tile_02 | **none** | 0.821 | - | 0.000 | - | two-sided |
| House04 | tile_wood01 | **none** | 0.812 | - | 0.000 | - | two-sided |
| House04 | tile_house01 | **none** | 0.807 | - | 0.000 | - | two-sided |
| House04 | tile_ston04 | **none** | 0.897 | - | 0.000 | - | two-sided |
| House04 | tile_wood03 | **none** | 0.903 | - | 0.000 | - | two-sided |
| House04 | tile_wood02 | **none** | 0.813 | - | 0.000 | - | two-sided |
| House04 | tile_windows01 | **none** | 0.831 | - | 0.000 | - | two-sided |
| House04 | bridge_01 | **none** | 0.722 | - | 0.000 | - | two-sided |
| House04 | tile_space01 | **none** | 1.000 | - | 0.000 | - | two-sided |
| House05 | tile_wood02 | **none** | 0.818 | - | 0.000 | - | two-sided |
| House05 | tile_wood01 | **none** | 0.818 | - | 0.000 | - | two-sided |
| House05 | ston02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| HouseEtc01 | c_wall04 | **none** | 0.724 | - | 0.000 | - | two-sided |
| HouseEtc01 | c_wall06 | **none** | 0.728 | - | 0.000 | - | two-sided |
| HouseEtc02 | tile_wood02 | **none** | 0.835 | - | 0.000 | - | two-sided |
| HouseEtc02 | tile_ston04 | **none** | 0.896 | - | 0.000 | - | two-sided |
| HouseEtc02 | c_wall04 | **none** | 0.731 | - | 0.000 | - | two-sided |
| HouseEtc02 | tile_wood03 | **none** | 0.904 | - | 0.000 | - | two-sided |
| HouseEtc02 | tile_house01 | **none** | 0.808 | - | 0.000 | - | two-sided |
| HouseEtc02 | tile_ston01 | **none** | 0.729 | - | 0.000 | - | two-sided |
| HouseEtc02 | horse_drawn_01 | **none** | 0.819 | - | 0.000 | - | two-sided |
| HouseEtc02 | tile_ston06 | **none** | 0.812 | - | 0.000 | - | two-sided |
| HouseEtc03 | steel_barred_a | **none** | 0.400 | - | 0.580 | - | cutout 0.25, two-sided |
| HouseEtc03 | steel_barred_door | **none** | 0.400 | - | 0.580 | - | cutout 0.25, two-sided |
| HouseWall01 | tile_wood01 | **none** | 0.813 | - | 0.000 | - | two-sided |
| HouseWall01 | tile_wood02 | **none** | 0.816 | - | 0.000 | - | two-sided |
| HouseWall01 | tile_ston04 | **none** | 0.894 | - | 0.000 | - | two-sided |
| HouseWall02 | tile_ston06 | **none** | 0.812 | - | 0.000 | - | two-sided |
| HouseWall02 | tile_wood01 | **none** | 0.823 | - | 0.000 | - | two-sided |
| HouseWall02 | tile_wood02 | **none** | 0.814 | - | 0.000 | - | two-sided |
| HouseWall02 | tile_ston04 | **none** | 0.898 | - | 0.000 | - | two-sided |
| HouseWall02 | light_02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| HouseWall03 | tile_wood02 | **none** | 0.818 | - | 0.000 | - | two-sided |
| HouseWall04 | tile_wood01 | **none** | 0.821 | - | 0.000 | - | two-sided |
| HouseWall04 | tile_wood02 | **none** | 0.819 | - | 0.000 | - | two-sided |
| HouseWall04 | tile_ston04 | **none** | 0.898 | - | 0.000 | - | two-sided |
| HouseWall05 | tile_wood02 | **none** | 0.821 | - | 0.000 | - | two-sided |
| HouseWall05 | tile_wood03 | **none** | 0.899 | - | 0.000 | - | two-sided |
| HouseWall06 | tile_wood02 | **none** | 0.820 | - | 0.000 | - | two-sided |
| HouseWall06 | tile_wood03 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Hunter01 | mu2 | **none** | 0.702 | - | 0.000 | - | two-sided |
| Jewel01 | mu2 | **none** | 0.180 | - | 0.000 | - | two-sided |
| Jewel02 | mu2 | **none** | 0.180 | - | 0.000 | - | two-sided |
| Jewel15 | mu2 | **none** | 0.180 | - | 0.000 | - | two-sided |
| Lich01 | mu2 | **none** | - | - | - | - | two-sided |
| Lich01 | bone | bone | 0.583 | 0.58 | 0.000 | 0.00 | two-sided |
| Lich01 | cloth | cloth | 0.721 | 0.72 | 0.000 | 0.00 | two-sided |
| Lich01 | skin | skin | 0.700 | 0.70 | 0.000 | 0.00 | two-sided |
| Mace01 | mu2 | **none** | - | - | - | - | two-sided |
| Mace01 | steel | steel | 0.321 | 0.32 | 1.000 | 1.00 | two-sided |
| Mace01 | wood | wood | 0.727 | 0.72 | 0.000 | 0.00 | two-sided |
| Mace02 | mu2 | **none** | - | - | - | - | two-sided |
| Mace02 | plate_steel | plate_steel | 0.694 | 0.34 | 1.000 | 1.00 | two-sided |
| Mace02 | steel | steel | 0.322 | 0.32 | 1.000 | 1.00 | two-sided |
| Mace02 | wood | wood | 0.721 | 0.72 | 0.000 | 0.00 | two-sided |
| ManBoots01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| ManGloves01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| ManHead01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| ManUpper01 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| MerchantAnimal01 | merchant_moster_a01 | **none** | 0.700 | - | 0.000 | - | two-sided |
| MerchantAnimal01 | merchant_moster_a02 | **none** | 0.700 | - | 0.000 | - | cutout 0.50, two-sided |
| MerchantAnimal02 | merchant_moster_b01 | **none** | 0.700 | - | 0.000 | - | two-sided |
| MixNpc01 | ob3 | **none** | 0.700 | - | 0.000 | - | two-sided |
| MixNpc01 | yellow_jewel | **none** | 1.000 | - | 0.000 | - | two-sided |
| NewFace01 | mu2 | **none** | 0.702 | - | 0.000 | - | cutout 0.25, two-sided |
| NewFace02 | mu2 | **none** | 0.701 | - | 0.000 | - | cutout 0.25, two-sided |
| NewFace03 | mu2 | **none** | 0.701 | - | 0.000 | - | cutout 0.25, two-sided |
| Object01 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object01 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object02 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object02 | filling_up02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object03 | flower02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object03 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object04 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object04 | flower02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object05 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object06 | so_grass | **none** | 0.900 | - | 0.000 | - | cutout 0.25, two-sided |
| Object06 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object07 | filling_up05 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object07 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object08 | flower3 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object09 | flower4 | **none** | 0.799 | - | 0.000 | - | two-sided |
| Object10 | filling_up04 | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| Object10 | filling_up03 | **none** | 0.826 | - | 0.000 | - | two-sided |
| Object10 | filling_up02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object10 | wood02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object103 | so_jgwall04 | **none** | 0.725 | - | 0.000 | - | two-sided |
| Object104 | so_jgwall02 | **none** | 0.728 | - | 0.000 | - | two-sided |
| Object107 | so_jgwall03 | **none** | 0.724 | - | 0.000 | - | two-sided |
| Object107 | so_jgwall05 | **none** | 0.859 | - | 0.000 | - | two-sided |
| Object11 | flower5 | **none** | 0.808 | - | 0.000 | - | two-sided |
| Object11 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object111 | door_01 | **none** | 0.724 | - | 0.000 | - | two-sided |
| Object112 | drogon_001 | **none** | 0.731 | - | 0.000 | - | two-sided |
| Object12 | flower | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object122 | stair_01 | **none** | 0.723 | - | 0.000 | - | two-sided |
| Object13 | flower | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object130 | angeflo_r | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object14 | flower | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object15 | so_jghwall02 | **none** | 0.732 | - | 0.000 | - | two-sided |
| Object15 | flower | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object16 | flower | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object17 | flower | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object18 | wood02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object18 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object18 | flower7 | **none** | 0.875 | - | 0.000 | - | two-sided |
| Object18 | flower9 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object18 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object18 | flower3 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object18 | flower02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object19 | flower7 | **none** | 0.875 | - | 0.000 | - | two-sided |
| Object19 | wood01 | **none** | 0.724 | - | 0.000 | - | two-sided |
| Object19 | filling_up05 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object19 | metal01 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Object20 | wd | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object21 | flower6 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object21 | wood03 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object22 | flower6 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object22 | flower4 | **none** | 0.796 | - | 0.000 | - | two-sided |
| Object23 | flower7 | **none** | 0.874 | - | 0.000 | - | two-sided |
| Object23 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object24 | flower7 | **none** | 0.874 | - | 0.000 | - | two-sided |
| Object24 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object25 | treea00 | **none** | 0.880 | - | 0.000 | - | two-sided |
| Object26 | treea03 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object27 | flower05 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object28 | treea02 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object28 | treea01 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object29 | treea00 | **none** | 0.881 | - | 0.000 | - | two-sided |
| Object30 | so_archb03 | **none** | 0.724 | - | 0.000 | - | two-sided |
| Object30 | treea00 | **none** | 0.881 | - | 0.000 | - | two-sided |
| Object30 | so_archb02 | **none** | 0.861 | - | 0.000 | - | two-sided |
| Object31 | wood002 | **none** | 0.883 | - | 0.000 | - | two-sided |
| Object31 | wood001 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Object32 | ston001 | **none** | 0.860 | - | 0.000 | - | two-sided |
| Object33 | ston001 | **none** | 0.863 | - | 0.000 | - | two-sided |
| Object34 | ston001 | **none** | 0.867 | - | 0.000 | - | two-sided |
| Object35 | song_bria01 | **none** | 0.731 | - | 0.000 | - | two-sided |
| Object35 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object35 | so_archb02 | **none** | 0.862 | - | 0.000 | - | two-sided |
| Object35 | flower03 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object36 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object36 | flower10 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object37 | flower2 | **none** | 0.900 | - | 0.000 | - | two-sided |
| Object38 | light01 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object39 | so_bob03 | **none** | 0.726 | - | 0.000 | - | two-sided |
| Object40 | htht01 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Object40 | yellow_jewel | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object40 | ob3 | **none** | 0.700 | - | 0.000 | - | two-sided |
| Object41 | ob3 | **none** | 0.700 | - | 0.000 | - | two-sided |
| Object42 | u2u3 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object43 | itp02 | **none** | 0.722 | - | 1.000 | - | cutout 0.25, two-sided |
| Object50 | gatewall041 | **none** | 0.731 | - | 0.000 | - | two-sided |
| Object50 | gatewall02 | **none** | 0.730 | - | 0.000 | - | two-sided |
| Object50 | gatelight01_r | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object65 | statueddown82 | **none** | 0.728 | - | 0.000 | - | two-sided |
| Object65 | statuedup72 | **none** | 0.730 | - | 0.000 | - | two-sided |
| Object65 | statuedsw052 | **none** | 0.740 | - | 0.000 | - | two-sided |
| Object65 | statuedeye_r | **none** | 1.000 | - | 0.000 | - | two-sided |
| Object80 | br001 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Object97 | so_jgtree01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| Object97 | so_jgtree02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object98 | so_jgtree01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| Object98 | so_jgtree02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Object99 | so_jgwall01 | **none** | 0.733 | - | 0.000 | - | two-sided |
| Object99 | so_jgwall02 | **none** | 0.728 | - | 0.000 | - | two-sided |
| Object99 | so_jgwall03 | **none** | 0.727 | - | 0.000 | - | two-sided |
| PantClass01 | mu2 | **none** | 0.720 | - | 0.000 | - | two-sided |
| PantClass02 | mu2 | **none** | 0.702 | - | 0.000 | - | two-sided |
| PantClass03 | mu2 | **none** | 0.780 | - | 0.000 | - | two-sided |
| PantElf01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| PantElf01 | bone | bone | 0.585 | 0.58 | 0.000 | 0.00 | cutout 0.50, two-sided |
| PantElf01 | leather | leather | 0.619 | 0.62 | 0.000 | 0.00 | cutout 0.50, two-sided |
| PantElf01 | skin | skin | 0.702 | 0.70 | 0.000 | 0.00 | cutout 0.50, two-sided |
| PantMale01 | mu2 | **none** | 0.465 | - | 1.000 | - | two-sided |
| PantMale03 | mu2 | **none** | - | - | - | - | two-sided |
| PantMale03 | brass | brass | 0.445 | 0.44 | 1.000 | 1.00 | two-sided |
| PantMale03 | cloth | cloth | 0.722 | 0.72 | 0.000 | 0.00 | two-sided |
| PantMale05 | mu2 | **none** | 0.585 | - | 0.000 | - | two-sided |
| PantMale06 | mu2 | **none** | 0.753 | - | 0.000 | - | two-sided |
| PantMale07 | mu2 | **none** | 0.554 | - | 1.000 | - | two-sided |
| PantMale08 | mu2 | **none** | 0.683 | - | 1.000 | - | two-sided |
| PantMale09 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| PantMale09 | brass | brass | 0.444 | 0.44 | 1.000 | 1.00 | cutout 0.50, two-sided |
| PantMale09 | plate_steel | plate_steel | 0.346 | 0.34 | 1.000 | 1.00 | cutout 0.50, two-sided |
| PantMale10 | mu2 | **none** | 0.346 | - | 1.000 | - | two-sided |
| Potion01 | mu2 | **none** | - | - | - | - | cutout 0.50, two-sided |
| Potion01 | foliage | foliage | 0.902 | 0.90 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Potion01 | fruit | fruit | 0.722 | 0.42 | 0.000 | 0.00 | cutout 0.50, two-sided |
| Potion02 | mu2 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Potion03 | mu2 | **none** | - | - | - | - | two-sided |
| Potion03 | brass | brass | 0.722 | 0.44 | 1.000 | 1.00 | two-sided |
| Potion03 | glass | glass | 0.722 | 0.12 | 0.000 | 0.00 | two-sided |
| Potion04 | mu2 | **none** | - | - | - | - | two-sided |
| Potion04 | brass | brass | 0.722 | 0.44 | 1.000 | 1.00 | two-sided |
| Potion04 | glass | glass | 0.722 | 0.12 | 0.000 | 0.00 | two-sided |
| Potion05 | mu2 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Potion06 | mu2 | **none** | - | - | - | - | two-sided |
| Potion06 | brass | brass | 0.722 | 0.44 | 1.000 | 1.00 | two-sided |
| Potion06 | glass | glass | 0.722 | 0.12 | 0.000 | 0.00 | two-sided |
| Potion07 | mu2 | **none** | - | - | - | - | two-sided |
| Potion07 | brass | brass | 0.722 | 0.44 | 1.000 | 1.00 | two-sided |
| Potion07 | glass | glass | 0.722 | 0.12 | 0.028 | 0.00 | two-sided |
| Scroll01 | mu2 | **none** | - | - | - | - | two-sided |
| Scroll01 | cloth | cloth | 0.800 | 0.72 | 0.000 | 0.00 | two-sided |
| Scroll01 | wood | wood | 0.725 | 0.72 | 0.000 | 0.00 | two-sided |
| Shield01 | mu2 | **none** | 0.721 | - | 0.000 | - | two-sided |
| Shield02 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Shield03 | mu2 | **none** | 0.735 | - | 0.000 | - | two-sided |
| Shield05 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Shield06 | mu2 | **none** | 0.740 | - | 0.000 | - | two-sided |
| Shield07 | mu2 | **none** | 0.584 | - | 0.000 | - | cutout 0.50, two-sided |
| Shield08 | mu2 | **none** | 0.685 | - | 1.000 | - | two-sided |
| Shield09 | mu2 | **none** | 0.744 | - | 0.000 | - | two-sided |
| Shield10 | mu2 | **none** | 0.342 | - | 1.000 | - | two-sided |
| Shield11 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Shield12 | mu2 | **none** | 0.472 | - | 1.000 | - | two-sided |
| Shield13 | mu2 | **none** | 0.469 | - | 1.000 | - | two-sided |
| Ship01 | TileGround03 | **none** | 0.730 | - | 0.000 | - | two-sided |
| Ship01 | ship01 | **none** | 0.817 | - | 0.000 | - | two-sided |
| Ship01 | ship03 | **none** | 0.821 | - | 0.000 | - | two-sided |
| Ship01 | ship04 | **none** | 0.722 | - | 0.000 | - | two-sided |
| Ship01 | ship07 | **none** | 0.816 | - | 0.000 | - | two-sided |
| Ship01 | ship05 | **none** | 0.817 | - | 0.000 | - | two-sided |
| Ship01 | ship06 | **none** | 0.800 | - | 0.000 | - | cutout 0.50, two-sided |
| Sign01 | notice | **none** | 0.801 | - | 0.000 | - | two-sided |
| Sign01 | signboard | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| Sign01 | doorknob | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| Sign02 | notice | **none** | 0.810 | - | 0.000 | - | two-sided |
| Skeleton01 | mu2 | **none** | 0.584 | - | 0.000 | - | two-sided |
| Smith01 | mu2 | **none** | 0.701 | - | 0.000 | - | two-sided |
| Spear02 | mu2 | **none** | - | - | - | - | two-sided |
| Spear02 | steel | steel | 0.318 | 0.32 | 1.000 | 1.00 | two-sided |
| Spear02 | wrought_iron | wrought_iron | 0.502 | 0.50 | 1.000 | 1.00 | two-sided |
| Spear03 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Spear08 | mu2 | **none** | - | - | - | - | two-sided |
| Spear08 | plate_steel | plate_steel | 0.722 | 0.34 | 1.000 | 1.00 | two-sided |
| Spear08 | steel | steel | 0.722 | 0.32 | 1.000 | 1.00 | two-sided |
| Spear09 | mu2 | **none** | - | - | - | - | two-sided |
| Spear09 | plate_steel | plate_steel | 0.722 | 0.34 | 1.000 | 1.00 | two-sided |
| Spear09 | steel | steel | 0.722 | 0.32 | 1.000 | 1.00 | two-sided |
| Spider01 | mu2 | **none** | 0.620 | - | 0.000 | - | cutout 0.50, two-sided |
| Staff01 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Staff02 | mu2 | **none** | 0.663 | - | 0.000 | - | cutout 0.50, two-sided |
| Staff03 | mu2 | **none** | 0.722 | - | 1.000 | - | two-sided |
| Staff04 | mu2 | **none** | - | - | - | - | two-sided |
| Staff04 | brass | brass | 0.722 | 0.44 | 1.000 | 1.00 | two-sided |
| Staff04 | glass | glass | 0.722 | 0.12 | 0.000 | 0.00 | two-sided |
| Stair01 | tile_wood01 | **none** | 0.807 | - | 0.000 | - | two-sided |
| SteelDoor01 | steel_barred_door | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| SteelDoor01 | steel_barred_b | **none** | 0.722 | - | 1.000 | - | two-sided |
| SteelStatue01 | tombstone_big | **none** | 0.732 | - | 0.000 | - | two-sided |
| SteelWall01 | steel_barred_a | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| SteelWall01 | steel_barred_b | **none** | 0.722 | - | 1.000 | - | two-sided |
| SteelWall02 | steel_barred_a | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| SteelWall02 | steel_barred_b | **none** | 0.722 | - | 1.000 | - | two-sided |
| SteelWall03 | steel_barred_a | **none** | 0.722 | - | 1.000 | - | cutout 0.50, two-sided |
| SteelWall03 | steel_barred_b | **none** | 0.722 | - | 1.000 | - | two-sided |
| Stone01 | ston01 | **none** | 0.860 | - | 0.000 | - | two-sided |
| Stone01 | ston02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Stone02 | ston01 | **none** | 0.864 | - | 0.000 | - | two-sided |
| Stone02 | ston02 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Stone03 | ston01 | **none** | 0.858 | - | 0.000 | - | two-sided |
| Stone04 | ston01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| Stone05 | ston01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| StoneGolem01 | ston1 | **none** | 0.862 | - | 0.000 | - | two-sided |
| StoneMuWall01 | bridge_01 | **none** | 0.725 | - | 0.000 | - | two-sided |
| StoneMuWall01 | c_wall04 | **none** | 0.726 | - | 0.000 | - | two-sided |
| StoneMuWall01 | c_wall05 | **none** | 0.732 | - | 0.000 | - | two-sided |
| StoneMuWall01 | c_wall06 | **none** | 0.735 | - | 0.000 | - | two-sided |
| StoneMuWall01 | tile_02 | **none** | 0.826 | - | 0.000 | - | two-sided |
| StoneMuWall02 | c_wall04 | **none** | 0.725 | - | 0.000 | - | two-sided |
| StoneMuWall02 | c_wall05 | **none** | 0.732 | - | 0.000 | - | two-sided |
| StoneMuWall02 | c_wall06 | **none** | 0.733 | - | 0.000 | - | two-sided |
| StoneMuWall03 | c_wall04 | **none** | 0.727 | - | 0.000 | - | two-sided |
| StoneMuWall03 | c_wall06 | **none** | 0.727 | - | 0.000 | - | two-sided |
| StoneMuWall04 | c_wall04 | **none** | 0.727 | - | 0.000 | - | two-sided |
| StoneMuWall04 | c_wall06 | **none** | 0.727 | - | 0.000 | - | two-sided |
| StoneMuWall04 | horse_drawn_01 | **none** | 0.815 | - | 0.000 | - | two-sided |
| StoneStatue01 | stone_statue02 | **none** | 0.732 | - | 0.000 | - | two-sided |
| StoneStatue02 | stone_statue01 | **none** | 0.734 | - | 0.000 | - | two-sided |
| StoneStatue03 | angel_stone_statue | **none** | 0.724 | - | 0.000 | - | two-sided |
| StoneStatue03 | tombstone_big | **none** | 0.727 | - | 0.000 | - | two-sided |
| StoneWall01 | bridge_01 | **none** | 0.725 | - | 0.000 | - | two-sided |
| StoneWall01 | tile_01 | **none** | 0.731 | - | 0.000 | - | two-sided |
| StoneWall01 | tile_02 | **none** | 0.826 | - | 0.000 | - | two-sided |
| StoneWall01 | tile_03 | **none** | 0.725 | - | 0.000 | - | two-sided |
| StoneWall02 | bridge_01 | **none** | 0.725 | - | 0.000 | - | two-sided |
| StoneWall02 | tile_01 | **none** | 0.731 | - | 0.000 | - | two-sided |
| StoneWall02 | tile_02 | **none** | 0.826 | - | 0.000 | - | two-sided |
| StoneWall02 | tile_03 | **none** | 0.725 | - | 0.000 | - | two-sided |
| StoneWall03 | tile_01 | **none** | 0.730 | - | 0.000 | - | two-sided |
| StoneWall03 | horse_drawn_01 | **none** | 0.815 | - | 0.000 | - | two-sided |
| StoneWall03 | tile_03 | **none** | 0.727 | - | 0.000 | - | two-sided |
| StoneWall04 | badge_01 | **none** | 0.784 | - | 0.000 | - | two-sided |
| StoneWall04 | badge_03 | **none** | 0.800 | - | 0.000 | - | cutout 0.50, two-sided |
| StoneWall06 | badge_01 | **none** | 0.784 | - | 0.000 | - | two-sided |
| StoneWall06 | badge_03 | **none** | 0.800 | - | 0.000 | - | cutout 0.50, two-sided |
| StoneWall06 | tile_01 | **none** | 0.727 | - | 0.000 | - | two-sided |
| Storage01 | mu2 | **none** | 0.780 | - | 0.000 | - | cutout 0.50, two-sided |
| Straw01 | grass_01 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Straw02 | grass_01 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| StreetLight01 | streetlight | **none** | 0.838 | - | 0.000 | - | two-sided |
| StreetLight01 | streetlight_brightness2 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Sword01 | mu2 | **none** | - | - | - | - | two-sided |
| Sword01 | brass | brass | 0.440 | 0.44 | 1.000 | 1.00 | two-sided |
| Sword01 | leather_wrap | leather_wrap | 0.775 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword01 | steel | steel | 0.325 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword02 | mu2 | **none** | - | - | - | - | two-sided |
| Sword02 | brass | brass | 0.466 | 0.44 | 1.000 | 1.00 | two-sided |
| Sword02 | leather_wrap | leather_wrap | 0.738 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword02 | steel | steel | 0.318 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword03 | mu2 | **none** | - | - | - | - | two-sided |
| Sword03 | leather_wrap | leather_wrap | 0.736 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword03 | plate_steel | plate_steel | 0.693 | 0.34 | 1.000 | 1.00 | two-sided |
| Sword03 | steel | steel | 0.309 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword04 | mu2 | **none** | - | - | - | - | two-sided |
| Sword04 | blade_steel | blade_steel | 0.220 | 0.18 | 0.349 | 0.35 | two-sided |
| Sword04 | brass | brass | 0.447 | 0.44 | 0.966 | 1.00 | two-sided |
| Sword04 | cloth | cloth | 0.722 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword04 | leather_wrap | leather_wrap | 0.757 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword05 | mu2 | **none** | - | - | - | - | two-sided |
| Sword05 | blade_steel | blade_steel | 0.227 | 0.18 | 0.349 | 0.35 | two-sided |
| Sword05 | brass | brass | 0.442 | 0.44 | 1.000 | 1.00 | two-sided |
| Sword05 | leather_wrap | leather_wrap | 0.749 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword06 | mu2 | **none** | - | - | - | - | two-sided |
| Sword06 | leather_wrap | leather_wrap | 0.757 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword06 | plate_steel | plate_steel | 0.698 | 0.34 | 1.000 | 1.00 | two-sided |
| Sword06 | steel | steel | 0.323 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword07 | mu2 | **none** | - | - | - | - | two-sided |
| Sword07 | brass | brass | 0.467 | 0.44 | 1.000 | 1.00 | two-sided |
| Sword07 | leather_wrap | leather_wrap | 0.752 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword07 | steel | steel | 0.321 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword08 | mu2 | **none** | - | - | - | - | two-sided |
| Sword08 | leather_wrap | leather_wrap | 0.740 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword08 | steel | steel | 0.322 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword09 | mu2 | **none** | - | - | - | - | two-sided |
| Sword09 | brass | brass | 0.468 | 0.44 | 1.000 | 1.00 | two-sided |
| Sword09 | steel | steel | 0.321 | 0.32 | 1.000 | 1.00 | two-sided |
| Sword16 | mu2 | **none** | - | - | - | - | two-sided |
| Sword16 | leather_wrap | leather_wrap | 0.776 | 0.72 | 0.000 | 0.00 | two-sided |
| Sword16 | painted_steel | painted_steel | 0.308 | 0.28 | 0.000 | 0.00 | two-sided |
| Tent01 | tile_house01 | **none** | 0.827 | - | 0.000 | - | two-sided |
| Tent01 | tile_ston06 | **none** | 0.829 | - | 0.000 | - | two-sided |
| Tomb01 | grave_02 | **none** | 0.738 | - | 0.000 | - | two-sided |
| Tomb02 | grave_01 | **none** | 0.734 | - | 0.000 | - | two-sided |
| Tomb03 | tombstone | **none** | 0.748 | - | 0.000 | - | two-sided |
| TreasureChest01 | treasure_chest | **none** | 0.824 | - | 0.000 | - | two-sided |
| TreasureDrum01 | drum | **none** | 0.700 | - | 0.039 | - | two-sided |
| Tree01 | tree | **none** | 0.881 | - | 0.000 | - | two-sided |
| Tree01 | tree_a | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Tree02 | tree | **none** | 0.880 | - | 0.000 | - | two-sided |
| Tree02 | Tree_a | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Tree03 | tree_03 | **none** | 0.880 | - | 0.000 | - | two-sided |
| Tree04 | tree_03 | **none** | 0.880 | - | 0.000 | - | two-sided |
| Tree05 | tree_03 | **none** | 0.882 | - | 0.000 | - | two-sided |
| Tree06 | tree_02 | **none** | 0.883 | - | 0.000 | - | two-sided |
| Tree07 | tree_03 | **none** | 0.878 | - | 0.000 | - | two-sided |
| Tree07 | tree_04 | **none** | 0.819 | - | 0.000 | - | two-sided |
| Tree08 | tree_01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| Tree09 | tree_07 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Tree10 | tree_07 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Tree11 | tree_06 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Tree11 | tree_03 | **none** | 0.881 | - | 0.000 | - | two-sided |
| Tree12 | tree_01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| Tree12 | tree_04 | **none** | 0.900 | - | 0.000 | - | cutout 0.50, two-sided |
| Tree13 | tree_05 | **none** | 0.900 | - | 0.000 | - | cutout 0.25, two-sided |
| Tree13 | tree_01 | **none** | 0.879 | - | 0.000 | - | two-sided |
| Waterspout01 | stone_statue02 | **none** | 0.735 | - | 0.000 | - | two-sided |
| Waterspout01 | reagon_waterspout | **none** | 0.729 | - | 0.000 | - | two-sided |
| Waterspout01 | ston01 | **none** | 0.864 | - | 0.000 | - | two-sided |
| Waterspout01 | ston02 | **none** | 1.000 | - | 0.000 | - | two-sided |
| Well02 | well | **none** | 0.837 | - | 0.000 | - | two-sided |
| Well02 | well__timber | **none** | 0.808 | - | 0.000 | - | two-sided |
| Well03 | jar_01 | **none** | 0.305 | - | 0.000 | - | two-sided |
| Well04 | jar_01 | **none** | 0.305 | - | 0.000 | - | two-sided |
| Wizard01 | mu2 | **none** | 0.780 | - | 0.000 | - | cutout 0.50, two-sided |

## 90 failures

- **Agon01/fur** — fur carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **BudgeDragon01/chitin** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **BudgeDragon01/chitin** — chitin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **BullFighter01/chitin** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **BullFighter01/chitin** — chitin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **BullFighter01/fur** — fur carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **Hound01/fur** — fur carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **Hound01/painted_steel** — ORM roughness 0.498 but painted_steel asks 0.28
- **Lich01/cloth** — alphaMode OPAQUE but 12% of the albedo's alpha is below 0.5; whatever was meant to be cut out is drawn solid
- **Lich01/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **ArmorElf01/leather** — ORM roughness 0.735 but leather asks 0.62
- **ArmorElf01/skin** — ORM roughness 0.780 but skin asks 0.70
- **ArmorElf01/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **ArmorMale01/plate_steel** — ORM roughness 0.463 but plate_steel asks 0.34
- **ArmorMale01/plate_steel** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **ArmorMale01/skin** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **ArmorMale01/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **ArmorMale03/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **ArmorMale06/leather** — ORM roughness 0.752 but leather asks 0.62
- **ArmorMale06/skin** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **ArmorMale06/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **ArmorMale08/plate_steel** — ORM roughness 0.681 but plate_steel asks 0.34
- **BootElf01/leather** — ORM roughness 0.743 but leather asks 0.62
- **BootElf01/leather** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **BootMale09/plate_steel** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **HelmMale05/skin** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **HelmMale05/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmMale06/leather** — ORM roughness 0.749 but leather asks 0.62
- **HelmMale06/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmMale07/plate_steel** — ORM roughness 0.553 but plate_steel asks 0.34
- **HelmMale07/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmMale08/plate_steel** — ORM roughness 0.683 but plate_steel asks 0.34
- **HelmMale08/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmMale09/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmMale10/plate_steel** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **PantElf01/leather** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **PantElf01/skin** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **PantElf01/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **PantMale09/plate_steel** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **Antidote01/brass** — ORM roughness 0.722 but brass asks 0.44
- **Antidote01/glass** — ORM roughness 0.722 but glass asks 0.12
- **Antidote01/glass** — glass carries relief 1.00 but its normal map leans 0.8deg under this part; the map is flat and the relief never reached it
- **Potion01/foliage** — foliage carries relief 1.00 and this slot has no normal map at all; the relief was authored and then dropped
- **Potion01/fruit** — ORM roughness 0.722 but fruit asks 0.42
- **Potion01/fruit** — alphaMode MASK but the albedo's alpha has no holes under this part; the discard costs four passes and cuts nothing
- **Potion01/fruit** — fruit carries relief 1.00 and this slot has no normal map at all; the relief was authored and then dropped
- **Potion03/brass** — ORM roughness 0.722 but brass asks 0.44
- **Potion03/glass** — ORM roughness 0.722 but glass asks 0.12
- **Potion03/glass** — glass carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **Potion04/brass** — ORM roughness 0.722 but brass asks 0.44
- **Potion04/glass** — ORM roughness 0.722 but glass asks 0.12
- **Potion04/glass** — glass carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **Potion06/brass** — ORM roughness 0.722 but brass asks 0.44
- **Potion06/glass** — ORM roughness 0.722 but glass asks 0.12
- **Potion06/glass** — glass carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **Potion07/brass** — ORM roughness 0.722 but brass asks 0.44
- **Potion07/glass** — ORM roughness 0.722 but glass asks 0.12
- **Potion07/glass** — glass carries relief 1.00 but its normal map leans 0.9deg under this part; the map is flat and the relief never reached it
- **Scroll01/cloth** — ORM roughness 0.800 but cloth asks 0.72
- **Arrows02/cloth** — ORM roughness 0.800 but cloth asks 0.72
- **Arrows02/leather** — ORM roughness 0.741 but leather asks 0.62
- **Axe02/steel** — ORM roughness 0.722 but steel asks 0.32
- **Axe03/plate_steel** — ORM roughness 0.692 but plate_steel asks 0.34
- **Axe06/plate_steel** — ORM roughness 0.724 but plate_steel asks 0.34
- **Axe06/steel** — ORM roughness 0.722 but steel asks 0.32
- **Axe07/steel** — ORM roughness 0.722 but steel asks 0.32
- **Bow01/cloth** — ORM roughness 0.800 but cloth asks 0.72
- **Bow02/cloth** — ORM roughness 0.800 but cloth asks 0.72
- **Mace02/plate_steel** — ORM roughness 0.694 but plate_steel asks 0.34
- **Spear08/plate_steel** — ORM roughness 0.722 but plate_steel asks 0.34
- **Spear08/steel** — ORM roughness 0.722 but steel asks 0.32
- **Spear09/plate_steel** — ORM roughness 0.722 but plate_steel asks 0.34
- **Spear09/steel** — ORM roughness 0.722 but steel asks 0.32
- **Staff04/brass** — ORM roughness 0.722 but brass asks 0.44
- **Staff04/glass** — ORM roughness 0.722 but glass asks 0.12
- **Staff04/glass** — glass carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **Sword01/leather_wrap** — ORM roughness 0.775 but leather_wrap asks 0.72
- **Sword03/plate_steel** — ORM roughness 0.693 but plate_steel asks 0.34
- **Sword05/blade_steel** — ORM roughness 0.227 but blade_steel asks 0.18
- **Sword06/plate_steel** — ORM roughness 0.698 but plate_steel asks 0.34
- **Sword16/leather_wrap** — ORM roughness 0.776 but leather_wrap asks 0.72
- **ArmorClass01/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmClass01/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmClass02/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **HelmClass03/hair** — ORM roughness 0.722 but hair asks 0.85
- **HelmClass03/skin** — ORM roughness 0.780 but skin asks 0.70
- **HelmClass03/skin** — skin carries relief 1.00 but its normal map leans 0.3deg under this part; the map is flat and the relief never reached it
- **House02/steel** — ORM roughness 0.722 but steel asks 0.32
- **charscene/cs_tilewater01_x3_tiling_hd_water_orm.png** — mean roughness 0.722 but water asks 0.08
- **noria/nr_tilewater01_x3_tiling_hd_water_orm.png** — mean roughness 0.722 but water asks 0.08

## 65 notes

Nothing renders wrong because of these.

- **Agon01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **BudgeDragon01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **BullFighter01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Hound01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Lich01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorElf01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorMale01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorMale03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorMale06/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorMale08/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorMale09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **BootElf01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **BootMale09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **GloveMale09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmMale05/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmMale06/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmMale07/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmMale08/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmMale09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmMale10/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **PantElf01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **PantMale03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **PantMale09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Antidote01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Potion01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Potion03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Potion04/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Potion06/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Potion07/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Scroll01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Arrows01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Arrows02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Axe01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Axe02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Axe03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Axe04/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Axe06/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Axe07/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Bow01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Bow02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **CrossBow01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **CrossBow03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **CrossBow04/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Mace01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Mace02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Spear02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Spear08/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Spear09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Staff04/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword04/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword05/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword06/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword07/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword08/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword09/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Sword16/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **ArmorClass01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmClass01/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmClass02/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **HelmClass03/mu2** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **Bridge01/bridge_shadow01** — a material slot no primitive draws with; the part list and the material list have drifted apart
- **BridgeStone01/bridge_shadow01** — a material slot no primitive draws with; the part list and the material list have drifted apart
