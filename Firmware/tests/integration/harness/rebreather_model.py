"""Closed-loop rebreather plant model for native_sim PID stability tests.

Reproduces the *overshoot* characteristic seen in real DiveCAN dive logs
(see ``log_backup/12-12-2025/``):

    Fire @ t=4749 s — 1.5 s solenoid pulse:
      baseline 0.701 bar → peak 0.778 bar → settled 0.707 bar
      overshoot above settled = +0.071 bar (≈71% of total rise)

A simple two-tau cascade (mixing lag → sensor lag) underpredicts this:
linear first-order lags can't peak above their steady-state input.  The
real loop has a sharp local concentration spike at the sensor location
during injection, which then redistributes through the rest of the
loop volume as the diver consumes O2 — that's a *2-compartment* mass
balance, not a 1-compartment lag.

Model topology
--------------

    solenoid open → +Q_inj LPM pure O2 ─┐
                                        ▼
                              ┌──────────────────┐
              metabolic O2 ←──│  LOCAL  V_local  │
              consumption     │  fraction f_l    │
                              │  (sensors here)  │
                              └──────────┬───────┘
                                         │  exchange flow Q_mix
                                         │  (proportional to f_l - f_b)
                                         ▼
                              ┌──────────────────┐
                              │  BULK   V_bulk   │
                              │  fraction f_b    │
                              └──────────────────┘

The sensors read ``f_l × ambient_pressure_bar`` through one first-order
sensor lag per cell.  When the solenoid fires, ``f_l`` jumps because
``V_local`` is small; the spike then bleeds into ``V_bulk`` at rate
``Q_mix``, which is what produces the dive-log-style overshoot.

Adding new characterised loops
------------------------------
``LoopProfile`` carries every parameter the model needs.  ``LOOP_PROFILES``
is a name → profile registry; appending a new entry is the only change
required to bring a new loop into the test matrix.  Document the source
(test rig, real dive log, vendor datasheet) in the ``source`` field so
future edits can revisit the numbers.

Sensor time constants are physical t63 (= τ for a first-order system).
When a datasheet quotes t90, convert with τ = t90 / 2.303 before
populating the profile.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from math import exp
from typing import List


# Sensor cell time constants taken from the QuickRecon DiveCAN
# characterisation notes:
#   - DiveO2 / O2S digital cells: t63 ≈ 2.0 s (electrochemistry only;
#     UART sampling cadence is modelled separately by the firmware's
#     poll loop, not in this plant model).
#   - Galvanic analog cells: t90 ≈ 6 s, so t63 ≈ 6 / ln(1/0.1)
#     = 6 / 2.303 ≈ 2.6 s.
SENSOR_TAU_DIGITAL_S: float = 2.0
SENSOR_TAU_ANALOG_S: float = 2.6


@dataclass
class LoopProfile:
    """One characterised rebreather loop's plant parameters.

    The 2-compartment model needs:
      - ``local_volume_l`` — the volume that "sees" the injection first
        (typically the path from injector → sensors → mouthpiece;
        ~1-2 L for a typical chest counterlung CCR).
      - ``bulk_volume_l`` — the rest of the breathing loop (scrubber
        canister volume, counterlung body, hoses past the sensors).
        Sets the long-tail mass balance.
      - ``mix_exchange_lpm`` — convective flow rate between local and
        bulk compartments.  Larger value → quicker equilibration →
        smaller overshoot.  Modulated empirically against dive-log
        overshoot ratios.

    Cell types must list exactly three entries in the dev_full topology
    order (cell 1, cell 2, cell 3).  Each entry's ``sensor_tau_s`` is
    the cell's electrochemical t63.
    """

    name: str
    source: str  # Where the numbers came from (test rig, real dive log, vendor)

    # Plant — 2 compartment
    local_volume_l: float          # Volume around the sensors / injector
    bulk_volume_l: float           # Rest of the loop
    mix_exchange_lpm: float        # Local↔bulk convective exchange flow

    # Disturbance / metabolic
    metabolic_lpm: float           # Diver O2 consumption (STP)

    # Actuator
    solenoid_lpm: float            # O2 injection flow when solenoid open (STP)

    # Sensors (one entry per cell in dev_full topology)
    sensor_tau_s_per_cell: List[float] = field(default_factory=list)

    # Operating point
    ambient_pressure_bar: float = 1.0    # 1 ATA = surface; >1 = at depth
    initial_o2_fraction: float = 0.21    # Pre-dive air
    diluent_o2_fraction: float = 0.21    # Diluent gas O2 fraction —
                                         # 0.21 for air, lower for
                                         # trimix / heliox at depth.


# ---------------------------------------------------------------------------
# Named profile registry — extend by appending entries.
#
# Numbers below come from analysing the QuickRecon dive log
# (log_backup/12-12-2025/, fire at t=4749 s):
#   baseline 0.701, peak 0.778, settled 0.707 → +0.077 bar peak above
#   baseline, +0.071 bar overshoot above settled, recovery τ ≈ 5-8 s.
# A 1.5 s pulse at ~5 LPM into ~1.5 L local volume at ~1 ATA gives
# ~+0.083 fraction at the local volume, matching the observed peak.
# The settling pattern back to baseline implies the bulk volume absorbs
# the spike with mix_exchange_lpm ~10 LPM equivalent.
#
# Replace these defaults with vendor-characterised values as more loops
# get profiled — the test code consumes the registry by name.
# ---------------------------------------------------------------------------


def _initial_fraction_for(setpoint_cb: int,
                          pressure_bar: float) -> float:
    """Return the O2 mole fraction that produces ``setpoint_cb`` PPO2
    at ``pressure_bar`` ambient.  Used as a sensible profile default
    so the closed-loop test starts at the operating point rather than
    being driven there from boot air during the warm-up window."""
    return (setpoint_cb / 100.0) / pressure_bar


# Plant parameter sets — one per name in the registry.  Loop volumes,
# mix exchange, metabolic and solenoid flows characterise the plant
# *physics*; depth and the resulting initial fraction are applied
# separately so the same plant can be exercised across the diving
# envelope (see ``LOOP_PROFILES`` below).
@dataclass
class LoopShape:
    local_volume_l: float
    bulk_volume_l: float
    mix_exchange_lpm: float
    metabolic_lpm: float
    solenoid_lpm: float
    source: str
    sensor_tau_s_per_cell: List[float] = field(default_factory=lambda: [
        SENSOR_TAU_DIGITAL_S,
        SENSOR_TAU_DIGITAL_S,
        SENSOR_TAU_ANALOG_S,
    ])
    diluent_o2_fraction: float = 0.21  # Air; profiles override for trimix


# Five shapes bracketing the operationally relevant range: typical
# moderate-work configuration, slow / fast plants (mixing extremes),
# resting / heavy-work diver demands.  All five are exercised at
# surface and shallow depth — the controller must be reliable across
# the entire diving envelope, not just at a sweet-spot pressure.
LOOP_SHAPES: dict[str, LoopShape] = {
    # Sizing notes for MK15-friendliness across the depth range:
    # MK15 averages 20% duty (1.5s ON / 6s OFF), so Q_inj_avg = 0.2 ×
    # solenoid_lpm.  For MK15 to drive PPO2 to 0.7 bar setpoint against
    # metabolic Q_met from diluent baseline f_dil×P_ambient, the per-
    # cycle gain must exceed the off-period diluent drain:
    #   solenoid × (1 − f_eq) × 1.5 s ≥ Q_met × (1 − f_dil) × 6 s
    # where f_eq = setpoint / P_ambient.  Surface (P=1) is the worst
    # case because f_eq is highest there.  At Q_met = 0.6, f_dil = 0.21,
    # setpoint 0.7 bar at surface: solenoid ≥ 6.3 LPM.  We set 8 LPM so
    # MK15 has headroom across the full surface→deep envelope.
    "typical": LoopShape(
        local_volume_l=1.5,
        bulk_volume_l=4.5,
        mix_exchange_lpm=10.0,
        metabolic_lpm=0.6,
        solenoid_lpm=8.0,
        source=(
            "QuickRecon log_backup/12-12-2025 — fire at t=4749 s "
            "(1.74 bar ambient) shows +0.077 bar peak from 1.5 s pulse, "
            "~6 s decay to settled.  Volumes chosen to bracket that "
            "response; metabolic + solenoid set so MK15 has headroom "
            "to track setpoint at surface (the worst-case depth)."
        ),
    ),
    "slow_plant": LoopShape(
        local_volume_l=2.0,
        bulk_volume_l=8.0,
        mix_exchange_lpm=4.0,
        metabolic_lpm=0.6,
        solenoid_lpm=8.0,
        source=(
            "bracket — large counterlung, slow exchange.  Stresses the "
            "phase-lag handling of both controllers without breaking "
            "MK15's mass-balance envelope."
        ),
    ),
    "fast_plant": LoopShape(
        local_volume_l=1.0,
        bulk_volume_l=3.0,
        mix_exchange_lpm=20.0,
        metabolic_lpm=0.6,
        solenoid_lpm=8.0,
        source=(
            "bracket — tight loop, well-mixed.  Local approaches bulk "
            "quickly so overshoot is minimal but the fast plant can "
            "still trip PID gains tuned for slower mixing."
        ),
    ),
    "resting": LoopShape(
        local_volume_l=1.5,
        bulk_volume_l=4.5,
        mix_exchange_lpm=10.0,
        metabolic_lpm=0.3,
        solenoid_lpm=8.0,
        source=(
            "bracket — resting / decompressing diver.  Low O2 demand "
            "lets MK15 saturate at setpoint with significant overshoot; "
            "PID needs to limit injection or it will overshoot too."
        ),
    ),
    "heavy_work": LoopShape(
        local_volume_l=1.5,
        bulk_volume_l=4.5,
        mix_exchange_lpm=10.0,
        metabolic_lpm=1.5,
        solenoid_lpm=8.0,
        source=(
            "bracket — working diver, high O2 demand.  At surface MK15 "
            "max injection (1.6 LPM avg) barely exceeds metabolic; "
            "leaves the controller with no headroom for transients. "
            "PID's variable duty cycle is the expected operating mode."
        ),
    ),
}


# Depth points to exercise each shape at.  Each entry is (ambient
# pressure, diluent O2 fraction): real CCR divers swap diluent gas
# with depth so the diluent's PPO2 contribution never exceeds safe
# limits at max depth.  Using air at 4 bar gives PPO2_dil = 0.84 bar
# (above setpoint!) which is operationally invalid — at depth the
# diluent is trimix or heliox with reduced f_dil.
DEPTH_POINTS: dict[str, tuple[float, float]] = {
    "surface": (1.0,  0.21),   # 1 ATA, air — pre-dive / dock checkout
    "shallow": (1.74, 0.21),   # ~7.4 m FW, air — matches the dive log
    "deep":    (4.0,  0.18),   # ~30 m FW, trimix 18/45 — typical
                               # rec depth limit; f_dil chosen so
                               # diluent PPO2 = 0.72 ≈ setpoint, the
                               # operating regime CCR divers tune for.
}


def _make_profile(shape_name: str, depth_name: str) -> LoopProfile:
    shape = LOOP_SHAPES[shape_name]
    pressure, f_dil = DEPTH_POINTS[depth_name]
    return LoopProfile(
        name=f"{shape_name}_{depth_name}",
        source=(
            f"{shape.source}  Depth: {depth_name} "
            f"({pressure} bar, diluent f={f_dil:.2f})"
        ),
        local_volume_l=shape.local_volume_l,
        bulk_volume_l=shape.bulk_volume_l,
        mix_exchange_lpm=shape.mix_exchange_lpm,
        metabolic_lpm=shape.metabolic_lpm,
        solenoid_lpm=shape.solenoid_lpm,
        sensor_tau_s_per_cell=list(shape.sensor_tau_s_per_cell),
        ambient_pressure_bar=pressure,
        initial_o2_fraction=_initial_fraction_for(70, pressure),
        diluent_o2_fraction=f_dil,
    )


# Materialised registry — every (shape × depth) combination is a named
# profile.  The test parametrize over the keys here; adding a new shape
# or depth automatically expands the matrix.
LOOP_PROFILES: dict[str, LoopProfile] = {
    f"{shape}_{depth}": _make_profile(shape, depth)
    for shape in LOOP_SHAPES
    for depth in DEPTH_POINTS
}


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------


class RebreatherModel:
    """Stateful 2-compartment plant model for closed-loop PID testing.

    Usage:
        m = RebreatherModel(profile=LOOP_PROFILES["typical"])
        for _ in range(N):
            time.sleep(dt)
            m.step(dt, solenoid_open=shim.get_solenoid_state()[0])
            # Push reported per-cell PPO2 back into the firmware via shim.

    State accessors return scalars in bar (PPO2) and dimensionless
    fractions; the test harness scales these to centibar / millivolts
    where required.
    """

    def __init__(self, profile: LoopProfile) -> None:
        self.profile = profile
        f0 = profile.initial_o2_fraction
        # Compartment O2 mole fractions (dimensionless 0..1).
        self._f_local: float = f0
        self._f_bulk: float = f0
        # Per-cell reported PPO2 in bar (after each cell's sensor lag).
        self._reported_ppo2: list[float] = [
            f0 * profile.ambient_pressure_bar
            for _ in range(3)
        ]

    # -- state accessors ----------------------------------------------------

    @property
    def true_local_ppo2(self) -> float:
        """PPO2 in the local (sensor-adjacent) compartment, before
        sensor lag.  This is what the cells are *trying* to track."""
        return self._f_local * self.profile.ambient_pressure_bar

    @property
    def true_bulk_ppo2(self) -> float:
        """PPO2 in the bulk compartment — useful for inspection but not
        directly observed by the cells."""
        return self._f_bulk * self.profile.ambient_pressure_bar

    @property
    def reported_ppo2(self) -> list[float]:
        """Per-cell PPO2 reading after each cell's electrochemical lag,
        in bar.  These are the values to inject back into the firmware."""
        return list(self._reported_ppo2)

    # -- integration --------------------------------------------------------

    def step(self, dt_s: float, solenoid_open: bool) -> None:
        """Advance the model by ``dt_s`` seconds.

        Mass balance on each compartment treats them as constant-volume
        well-stirred reactors at the configured ``ambient_pressure_bar``.
        Loop volume is held constant by either:

        * **diluent makeup** when injection alone can't keep up with the
          diver's pure-O2 demand (``Q_dil = max(0, Q_met - Q_inj_local)``);
          the makeup gas enters at the diluent fraction (typically air,
          0.21) so it contributes some PPO2 baseline at depth.
        * **overpressure venting** when sustained injection exceeds the
          metabolic demand (``Q_vent = max(0, Q_inj_local - Q_met)``);
          the vented gas leaves at the current local fraction.

        Without these terms the math would let f_l drift below the
        diluent fraction during long-running deficits and cap at
        ~Q_inj/(Q_inj+Q_met) during sustained injection — neither
        matches real CCR behaviour.  Adding them lets MK15 actually
        drive PPO2 toward ambient pressure (the bang-bang algorithm's
        natural operating envelope) and the resulting closed-loop
        track-the-setpoint behaviour matches what the dive logs show.

        All flows are L/min; converted to L/s before integration.
        """
        p = self.profile

        # All flows on the wire are in STP volume (L/min STP).  To turn
        # them into compartment-fraction change rates we divide by the
        # compartment's *actual* gas volume at ambient pressure, which
        # is V_compartment_L × P_ambient_bar in STP units.  Equivalently:
        # the "effective" flow rate seen by the fraction balance is
        # Q_STP / P_ambient.  Without this scaling, the model
        # over-predicts per-fire ΔPPO2 by a factor of P at depth.
        q_inj_lps = (p.solenoid_lpm / 60.0) if solenoid_open else 0.0
        q_met_lps = p.metabolic_lpm / 60.0
        q_mix_lps = p.mix_exchange_lpm / 60.0

        # Scale to actual-volume terms.
        p_amb = p.ambient_pressure_bar
        q_inj_eff = q_inj_lps / p_amb
        q_met_eff = q_met_lps / p_amb
        q_mix_eff = q_mix_lps / p_amb

        # Volume balance for the LOCAL compartment.  The metabolic
        # outflow is "pure O2 removed by the body" (rate Q_met,
        # independent of f_l) — same volumetric magnitude must be
        # replaced for the loop to hold constant volume.  Solenoid is
        # the first source; the rest is made up from diluent (entering
        # the local compartment for simplicity, since the breathing
        # path runs from counterlung through sensors and the makeup
        # gas reaches both volumes via the mixing flow anyway).
        if q_inj_eff >= q_met_eff:
            # Excess gas vents (counterlung OPV releases at f_l).
            q_dil_eff = 0.0
            q_vent_eff = q_inj_eff - q_met_eff
        else:
            q_dil_eff = q_met_eff - q_inj_eff
            q_vent_eff = 0.0

        # O2 inflow to local: solenoid (pure) + diluent (at f_dil).
        # O2 outflow from local: metabolic (pure) + vent (at f_l).
        # Effective flows are STP volume / ambient pressure, in actual-
        # volume units; dividing by V_local (also actual volume) gives
        # the correct fraction-change rate at any depth.
        df_local = (
            q_inj_eff * 1.0
            + q_dil_eff * p.diluent_o2_fraction
            - q_met_eff * 1.0
            - q_vent_eff * self._f_local
            + q_mix_eff * (self._f_bulk - self._f_local)
        ) / p.local_volume_l

        # Bulk compartment derivative — only the convective exchange
        # changes its O2 content (no injection / metabolism / diluent
        # in the bulk in this topology — they all act on local).
        df_bulk = (
            q_mix_eff * (self._f_local - self._f_bulk)
        ) / p.bulk_volume_l

        # Euler integration is sufficient given the ~25 ms test timestep
        # vs the seconds-scale time constants of the plant.
        self._f_local += df_local * dt_s
        self._f_bulk += df_bulk * dt_s

        # Clamp to physical range — guards against a degenerate test
        # configuration (e.g. metabolic >> max injection) producing
        # negative fractions and confusing the assertion.
        self._f_local = max(0.0, min(1.0, self._f_local))
        self._f_bulk = max(0.0, min(1.0, self._f_bulk))

        # Per-cell sensor lag — discrete first-order solution
        #   y[k+1] = y[k] + (u - y[k]) * (1 - exp(-dt/τ))
        # which is stable for any dt (vs Euler which diverges if dt > 2τ).
        target = self.true_local_ppo2
        for idx, tau in enumerate(p.sensor_tau_s_per_cell):
            alpha = 1.0 - exp(-dt_s / max(tau, 1e-6))
            self._reported_ppo2[idx] += (target - self._reported_ppo2[idx]) * alpha
