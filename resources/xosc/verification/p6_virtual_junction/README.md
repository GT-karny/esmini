# P6 Virtual-Junction VD verification scenes (S7b)

VirtualDriver routing scenes over the **native OpenDRIVE virtual junction**
(P6, design `GT_esmini/docs/archive/odr_1619_program/odr_p6_virtual_junction_design.md` §4/§6 S7b).

Map: `resources/xodr/virtual_junction_23.xodr` — a copy of the P6 S2 authoritative
fixture `GT_esmini/test/odr_fixtures/handauthored/23_virtual_junction_17.xodr`
(provenance recorded in the xodr header). Main road 1 stays unsplit; branch road 2
attaches mid-road at the anchor s=100; virtual junction 888 spans road 1 [95,105];
road 3 continues from road 2.

## Scenes

| Scene | Route | Asserts (telemetry matchers) |
| :-- | :-- | :-- |
| `vj_branch_turn` | road 1 (s=10, lane −1) → **road 3** (s=40) | reaches **road 3** (`lane_keep road_id=3`, late window — road id, not junction id), never stalls (`min_speed_above`), speed sane (`speed_below`). Departs main road at anchor s=100 onto the −45° branch. |
| `vj_straight_through` | road 1 (s=10) → road 1 (s=180) | stays on **road 1 / lane −1** the whole run (`lane_keep`) — T2 pass-through; never departs onto the branch; never stalls; speed sane. |
| `vj_followroute_smoke` | road 1 (s=10) → road 3 (s=40), **ControllerFollowRoute** | headless esmini smoke (deferred from S6): the anchor-aware `LaneIndependentRouter` plans across the VJ (waypoint 1 synthesized on road 2) and the ego reaches road 3. Verified from the recorded run. |

Batch manifest: `resources/xosc/verification/p6_vj_batch.yaml`. Telemetry goldens:
`GT_esmini/test/telemetry_goldens/p6_vj/`.

## HARD RULE — no junction-id assertions on road 1's span

Per design §6 S7b + the membership decision (§10.2): **no expectation asserts a
junction id or junction membership anywhere on main road 1's span**. In v1 the
main-road span deliberately reports `GetJunctionId = -1` / `IsInJunction = false`
(interpretive; the question is surfaced upstream in issue #592 / PR-B). "Reached
road 3" is therefore asserted by **OpenDRIVE road id** (a road id is not a junction
id), and stay-on-main is asserted by **road id 1** — both lawful. Do not add a
junction-membership assertion on road 1 here.

## Run

```
DriverScript/.venv/Scripts/python.exe GT_esmini/scripts/verification/gt_sim_test.py \
    batch resources/xosc/verification/p6_vj_batch.yaml --out test_results/p6_vj
```

To register results in the web annotation UI, run with `--out test_results/web/p6_vj`
(optional; the annotation-UI wiring is out of scope for S7b).
