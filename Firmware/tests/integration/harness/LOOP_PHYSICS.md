# Rebreather Loop Physics — Modelling Notes

Working notes from the closed-loop PID stability simulator
(`rebreather_model.py`).  The simulator is open to characterising new
loops by adding profiles to `LOOP_PROFILES`; this document captures the
data and reasoning behind the defaults so future profiles can be
calibrated against the same evidence.

## Sources

### 1. Real dive log (`log_backup/12-12-2025/`)

QuickRecon DiveCAN unit, ~2 h dive, 110 solenoid fires logged in
`LOG.TXT`.  Cross-referenced with `EVENTS.CSV` PPO2 samples
(consensus at 500 ms cadence) and `PPO2_ATMOS` for ambient pressure.

Solenoid pulse duration: **uniformly 1.5 s** (only fire length seen
across the entire dive).  Hardware delivers a fixed pulse via the
PID's PWM duty mapping plus minimum-on-time clamping.

### 2. Sensor specs (vendor / characterisation)

| Cell type    | Time constant | Notes                                        |
|--------------|---------------|----------------------------------------------|
| DiveO2       | t63 ≈ 2.0 s   | Electrochemistry only; UART sampling separate |
| OxygenScientific O2S | t63 ≈ 2.0 s | As above; ~500 ms protocol sampling on top |
| Galvanic (analog) | t90 ≈ 6 s → t63 ≈ 2.6 s | First-order assumption (`τ = t90 / ln 10 ≈ t90 / 2.303`) |

The model treats every cell as a first-order lag on the true local
PPO2 — adequate for the seconds-scale dynamics the PID has to handle.
Higher-order sensor effects (digital cells' DSP smoothing, galvanic
temperature dependence) are not modelled.

## Key empirical finding: substantial overshoot

Isolated fire at **t = 4749 s** (chosen because >30 s gap from
neighbouring fires, so the response is uncontaminated):

| Quantity                          | Value                  |
|-----------------------------------|------------------------|
| Ambient pressure                  | 1.74 bar (~7.4 m FW)   |
| Baseline PPO2                     | 0.701 bar              |
| Peak PPO2 (within ~2 s of fire)   | 0.778 bar              |
| Settled PPO2 (~30 s after)        | 0.707 bar              |
| Peak above baseline               | **+0.077 bar**         |
| Overshoot above settled           | **+0.071 bar (≈92% of total rise)** |
| Time to peak from fire start      | 2.2 s                  |

In other words, a 1.5 s injection pulse pushes PPO2 to a *peak* that's
71 mbar above where it'll eventually settle.  The sensors see this
spike before the gas redistributes through the rest of the loop.

This overshoot **cannot be modelled by a single first-order mixing
lag** — first-order step responses can't peak above their input.
Reproducing the dive-log behaviour requires either (a) a model with
explicit injection-plume dynamics, or (b) a multi-compartment loop
model with sensors in the compartment that receives the injection.

### Mass-balance check (at the dive conditions above)

For a 5 LPM injection into a 1.5 L local volume at 1.74 bar
ambient, baseline fraction 0.403, 1.5 s pulse:

```
df/dt = Q_inj · (1 − f) / V_local
      = (5 / 60) · (1 − 0.403) / 1.5
      ≈ 0.033 s⁻¹

Δf over 1.5 s pulse, integrating with f rising:  ≈ +0.04
peak local PPO2 ≈ (0.403 + 0.04) · 1.74  ≈  0.77 bar
```

That matches the observed peak of 0.778 bar to within model parameter
uncertainty.  The smoke test in `rebreather_model.py`'s docstring
confirms the 2-compartment model produces a comparable spike.

## Model topology

```
solenoid open → +Q_inj LPM pure O2 ─┐
                                    ▼
                          ┌──────────────────┐
          metabolic O2 ←──│  LOCAL  V_local  │
          consumption     │  fraction f_l    │
                          │  (sensors here)  │
                          └──────────┬───────┘
                                     │  exchange flow Q_mix
                                     │  (proportional to f_l − f_b)
                                     ▼
                          ┌──────────────────┐
                          │  BULK   V_bulk   │
                          │  fraction f_b    │
                          └──────────────────┘

         per-cell first-order sensor lag (τ_cell)
                          │
                          ▼
                    reported PPO2[N]
```

Sensors read **the local compartment**.  Injection drives `f_l` quickly
upward in the small volume; the spike then bleeds into the bulk over
seconds via `Q_mix`.  Diver metabolism removes O2 from the local
compartment (the diver breathes from where the sensors are).

### Parameter intuition

| Parameter         | Effect on response                                       |
|-------------------|----------------------------------------------------------|
| `local_volume_l`  | Smaller → larger peak per pulse (less dilution at sensors) |
| `bulk_volume_l`   | Larger → sustained overshoot, slower decay to settled    |
| `mix_exchange_lpm`| Larger → faster local↔bulk equilibration → smaller overshoot |
| `metabolic_lpm`   | Higher → settled PPO2 trends down without firing         |
| `solenoid_lpm`    | Higher → bigger peak per pulse, less PID stability       |
| `sensor_tau_s`    | Higher → phase lag at sensor; can destabilise PID        |

## Calibration against dive data

The `"typical"` profile is tuned to bracket the t=4749 fire response:

| `LoopProfile` field     | Value      | Justification                       |
|-------------------------|-----------:|-------------------------------------|
| `local_volume_l`        | 1.5 L      | Mouthpiece + hose + sensor housing  |
| `bulk_volume_l`         | 4.5 L      | Counterlung + scrubber canister     |
| `mix_exchange_lpm`      | 10 LPM     | Tuned so spike decays in ~6 s       |
| `metabolic_lpm`         | 1.5 LPM    | Typical moderate work               |
| `solenoid_lpm`          | 5 LPM      | Common CCR injection rate           |
| `ambient_pressure_bar`  | 1.0 (default) | Surface — override for depth tests  |
| `initial_o2_fraction`   | 0.21       | Air                                 |

The other profiles (`slow_plant`, `fast_plant`, `resting`,
`heavy_work`) bracket the four axes the user identified as
operationally relevant: loop volume, exchange rate, breathing rate,
and injection flow.

## Volume balance — diluent makeup and venting

Real CCR loops conserve gas volume actively.  When the diver consumes
O2 faster than the solenoid replaces it, an automatic diluent valve
(ADV) admits diluent gas to keep the counterlung from collapsing.
When the solenoid pumps in more than the diver consumes, the
overpressure valve (OPV) vents the excess.  These two flows are
critical to MK15 stability: without them, the simple mass balance
``df/dt = Q_inj * (1 − f) − Q_met * f`` produces an equilibrium
``f = Q_inj / (Q_inj + Q_met)`` that's far below the operating PPO2,
because the solenoid term self-saturates as ``f`` approaches 1.

The model implements both:

```python
Q_dil  = max(0, Q_met - Q_inj_inst)   # diluent fills volume deficit
Q_vent = max(0, Q_inj_inst - Q_met)   # excess vents at f_local
```

O2 mass balance on the local compartment:

```
df_local/dt = (
      Q_inj * 1                  # pure-O2 solenoid inflow
    + Q_dil * f_dil              # diluent inflow at diluent fraction
    - Q_met * 1                  # diver removes pure O2
    - Q_vent * f_local           # OPV releases at current fraction
    + Q_mix * (f_bulk - f_local) # convective exchange to bulk
) / V_local
```

With these terms, sustained ``Q_inj > Q_met`` drives the loop toward
``f → 1`` (pure O2), bounded by the solenoid's max flow rate.  MK15's
bang-bang algorithm can therefore reach any PPO2 ≤ ``solenoid_lpm * P``
provided its 20%-duty mean injection ≥ metabolic demand.

### Surface-vs-depth dimensioning

MK15's worst case is **surface (1 ATA)** because the diluent baseline
contributes only ``f_dil × 1 = 0.21`` bar PPO2 — the solenoid has to
bridge the entire remaining 0.49 bar to setpoint.  At depth, the
diluent contribution grows with ambient pressure (1.74 bar gives 0.37
bar from air alone) and the solenoid has less work to do.

For MK15 to track a 0.7 bar setpoint at surface:

```
solenoid_lpm * (1 − f_eq) * 1.5s ≥ Q_met * (1 − f_dil) * 6s
                                                      ^ off period
                                       ^ where MK15 idles and the diluent
                                         drains O2 fraction toward f_dil
```

With ``Q_met = 0.6 LPM`` and ``f_dil = 0.21``, this requires
``solenoid_lpm ≥ 6.3 LPM``.  The default profiles set 8 LPM so MK15
has headroom across the full surface→deep envelope.

## STP flow → fraction-change scaling at depth

Solenoid flow `Q_inj` and metabolic demand `Q_met` are specified in
**STP volume per minute** (the convention vendors use for solenoid
orifice specs and diver metabolism).  The compartment volumes
(`V_local`, `V_bulk`) are **actual loop volumes** — the physical
counterlung + hose volume, independent of depth.

To compute a fraction-change rate (`df/dt`, dimensionless per second)
from an STP flow, the model converts to actual-volume terms by
dividing by ambient pressure:

```
Q_eff = Q_STP / P_ambient_bar         # actual volumetric flow rate
df/dt ∝ Q_eff / V_compartment
```

Without this scaling the model would over-predict per-fire PPO2
swings by a factor of `P_ambient` at depth.  A 1.5 s solenoid fire of
5 LPM into a 1.5 L local volume should produce ~9 cb ΔPPO2 at 4 bar
ambient (the back-of-envelope: 0.125 L STP / (1.5 L actual × 4 bar)
× P_ambient ≈ 0.083 bar), not 27 cb.  The latter would predict
catastrophic over-injection at recreational depths — operationally
nonsensical, and not what real CCR loops do.

## Depth-dependent diluent gas mix

Real CCR divers swap diluent gas with depth so the diluent's PPO2
contribution stays within safe operating limits at max depth:

| Depth        | Typical f_dil       | Diluent PPO2       | Comment                          |
|--------------|---------------------|--------------------|----------------------------------|
| Surface      | 0.21 (air)          | 0.21 bar           | Pre-dive checkout                |
| Shallow ~7 m | 0.21 (air)          | 0.36 bar           | Stays below max safe PPO2        |
| Deep ~30 m   | 0.18 (trimix 18/45) | 0.72 bar           | Tuned so dil PPO2 ≈ setpoint     |

The model's `DEPTH_POINTS` registry pairs each depth with an
appropriate diluent fraction.  Combining shallow operating parameters
with air diluent at 4 bar would put the loop's "no-injection" baseline
PPO2 above setpoint, which the solenoid can't fix and would never
actually happen in the field.

## Known limitations

The current model **omits** the following effects.  None of them
break the "won't oscillate" contract; they matter for higher-fidelity
testing of disturbance rejection or extreme operating points:

1. **Workload-driven metabolic rate** — the model uses a constant
   ``metabolic_lpm``.  Real divers vary O2 consumption with workload;
   a step-response test would need a piecewise schedule.

2. **Gas mixing time within a compartment** — both compartments are
   treated as well-stirred.  Real counterlungs have internal flow
   patterns that delay equilibration.

3. **Tidal-volume breathing** — the model treats consumption as
   continuous.  Real breathing is pulsatile, which can briefly
   underflow the counterlung and trigger ADV bursts even when steady
   state is balanced.

4. **Hyperoxic / hypoxic cell behaviour** — sensors are modelled as
   linear first-order.  At PPO2 > 2 bar or < 0.1 bar the galvanic
   electrochemistry deviates; well outside the operational envelope.

5. **Sensor sampling-rate vs cell time-constant interaction** — the
   per-cell sensor lag captures electrochemistry only.  The firmware's
   DiveO2 / O2S poll cadence (~100 ms / ~500 ms) adds additional phase
   lag at the controller's input.  Not yet modelled separately.

## How to add a newly characterised loop

1. Pick a representative isolated fire from the unit's dive log (≥30 s
   isolation from neighbouring fires).
2. Extract: baseline PPO2, peak PPO2, settled PPO2, ambient pressure,
   time-to-peak.
3. Solve for `(local_volume_l, bulk_volume_l, mix_exchange_lpm)` that
   reproduces those values when the smoke test in
   `rebreather_model.py`'s docstring is re-run.
4. Add a new entry to `LOOP_PROFILES` with a clear `source` string
   referencing the log file and fire timestamp.
5. Optionally extend the `@pytest.mark.parametrize` list in
   `test_pid_stability.py` to include the new profile in the test
   matrix (this is the only test-code change required).
