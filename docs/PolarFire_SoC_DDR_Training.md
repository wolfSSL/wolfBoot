# PolarFire SoC DDR Training Investigation

## Overview

This document tracks the investigation and debugging of LPDDR4 training on the PolarFire SoC MPFS250T Video Kit board. The DDR controller uses a Training IP (TIP) block that performs automatic training phases, but requires specific initialization sequences and state transitions to progress correctly.

**Target Hardware:**
- Board: PolarFire SoC Video Kit
- SoC: MPFS250T-FCG1152
- Memory: Micron MT53D512M32D2DS-053 LPDDR4 (2GB, x32, 1600 Mbps)
- Memory Type: LPDDR4 (not DDR4 - board has both, but MSS uses LPDDR4)

**Current Status:** TIP training gets stuck after BCLK_SCLK phase completes. Write Leveling (WRLVL), Read Gate Training (RDGATE), and Read Data Eye Training (DQ_DQS) phases do not start automatically.

---

## DDR Controller Architecture

### Key Components

1. **DDR Controller (DDRCFG_BASE @ 0x20084000)**
   - Main controller logic
   - DFI (DDR PHY Interface) control
   - Mode Register (MR) write interface
   - Memory Test Controller (MTC)

2. **DDR PHY (CFG_DDR_SGMII_PHY @ 0x20080000)**
   - Physical layer interface
   - Training IP (TIP) state machine
   - Expert mode for manual control
   - Per-lane training registers

3. **Training IP (TIP)**
   - Hardware state machine for automatic training
   - Phases: BCLK_SCLK, ADDCMD, WRLVL, RDGATE, DQ_DQS
   - Updates `training_status` register as phases complete
   - Requires specific conditions to transition between phases

### Critical Registers

**PHY Registers:**
- `PHY_TRAINING_STATUS` (0x804): Bit flags for completed training phases
  - Bit 0: BCLK_SCLK complete
  - Bit 1: ADDCMD complete
  - Bit 2: WRLVL complete
  - Bit 3: RDGATE complete
  - Bit 4: DQ_DQS complete
- `PHY_TRAINING_SKIP` (0x808): Which phases to skip (0x02 = skip ADDCMD)
- `PHY_TRAINING_RESET` (0x80C): Reset TIP state machine
- `PHY_TRAINING_START` (0x810): Start/stop TIP
- `PHY_GT_STATE` (0x82C): Gate training state (0xB is normal, not error)
- `PHY_WL_DELAY_0` (0x830): Write leveling delay for lane 0
- `PHY_EXPERT_MODE_EN` (0x850): Enable expert mode for manual control
- `PHY_DPC_BITS` (0x5C0): DPC configuration (vrgen_h for WRLVL)

**Controller Registers:**
- `MC_DFI_INIT_START` (0x00): Start DFI initialization
- `MC_DFI_INIT_COMPLETE` (0x04): DFI initialization complete flag
- `MC_CTRLR_INIT` (0x08): Controller initialization trigger
- `MC_INIT_MR_ADDR` (0x20): Mode register address for MR writes
- `MC_INIT_MR_WR_DATA` (0x24): Mode register write data
- `MC_INIT_MR_W_REQ` (0x28): Mode register write request

---

## LPDDR4 Training Sequence

### Standard Sequence (Per User Guide)

1. **DFI Initialization**
   - Release training reset
   - Start DFI init (`DFI_INIT_START`)
   - Wait for `DFI_INIT_COMPLETE`

2. **DRAM Initialization (LPDDR4)**
   - Device reset sequence
   - PLL frequency doubling for MR writes
   - Mode Register writes (MR1, MR2, MR3, MR4, MR11, MR12, MR13, MR14, MR16, MR17, MR22)
   - PLL frequency restore
   - CA VREF training (manual)
   - ADDCMD training (manual)
   - MR re-write after ADDCMD
   - ZQ calibration

3. **TIP Training Phases** (automatic)
   - BCLK_SCLK: Clock training
   - ADDCMD: Command/Address training (can be skipped if done manually)
   - WRLVL: Write leveling
   - RDGATE: Read DQS gate training
   - DQ_DQS: Read data eye training

4. **Post-Training**
   - Write calibration (using MTC)
   - Memory test

### LPDDR4-Specific Requirements

**Mode Register Values (MT53D512M32D2DS-053 @ 1600 Mbps):**
- MR1 = 0x56: nWR=16, RD preamble=toggle, WR preamble=2tCK, BL=16
- MR2 = 0x2D: RL=14, WL=8, WLS=1 (normal mode)
- MR2 = 0xAD: RL=14, WL=8, WLS=1, **WRLVL enable (bit 7=1)** - Required for WRLVL phase
- MR3 = 0xF1: PDDS=RZQ/6 (40ohm), DBI-RD/WR disabled
- MR11 = 0x31: DQ_ODT=RZQ2, CA_ODT=RZQ4
- MR12 = 0x32: CA VREF=50
- MR13 = 0x00: FSP-OP=0, FSP-WR=0, DMI enabled
- MR14 = 0x0F: DQ VREF=15
- MR22 = 0x06: SOC_ODT=RZQ6 (40ohm)

**WRLVL Configuration:**
- `DPC_BITS` vrgen_h = 0x5 (bits 9:4) - Set before training reset release
- `RPC3_ODT` = 0x0 - ODT off during WRLVL
- MR2 bit 7 = 1 - Enable WRLVL mode in DRAM

**IBUFMD Registers (LPDDR4 specific):**
- `PHY_RPC_IBUFMD_ADDCMD` = 0x3
- `PHY_RPC_IBUFMD_CLK` = 0x4
- `PHY_RPC_IBUFMD_DQ` = 0x3
- `PHY_RPC_IBUFMD_DQS` = 0x4

---

## Our Implementation vs HSS

### HSS (Hart Software Services) - Working Reference

**Sequence:**
1. Configure PHY for WRLVL (vrgen_h=0x5, ODT=0) **before** training reset release
2. Release training reset
3. Start DFI init
4. Wait for DFI init complete
5. Call `lpddr4_manual_training()`:
   - Device reset
   - PLL freq double
   - MR writes
   - PLL freq restore
   - CA VREF training
   - ADDCMD training
   - MR re-write
   - ZQ calibration
6. **State machine transitions:**
   - Check BCLK_SCLK complete → go to ADDCMD state
   - Check ADDCMD skipped → immediately go to WRLVL state
   - Poll `training_status` for WRLVL bit
7. TIP automatically completes WRLVL, RDGATE, DQ_DQS
8. Restore ODT and disable WRLVL in MR2

**Key Observation from HSS Logs:**
- Line 120: `END lpddr4_manual_training training_status: 00000001` (BCLK_SCLK only)
- Line 128: `POST_MANUAL_TRAINING training_status: 0000001D` (all phases done!)
- Lines 134-138: `wl_delay` values already populated

**Conclusion:** TIP runs automatically between manual training end and POST check. No explicit restart needed.

### Our Implementation - Current State

**Sequence:**
1. ✅ Configure PHY for WRLVL (vrgen_h=0x5, ODT=0) before training reset release
2. ✅ Release training reset
3. ✅ Start DFI init
4. ✅ Wait for DFI init complete
5. ✅ Manual LPDDR4 training (matches HSS)
6. ✅ ZQ calibration
7. ✅ **State machine simulation:**
   - Check BCLK_SCLK done
   - Check ADDCMD skipped
   - Enable MR2 WRLVL
   - Set training_start=1
   - Add delays
8. ❌ **Wait for TIP - STUCK HERE**
   - `training_status` stays at 0x1 (BCLK_SCLK only)
   - `wl_delay` remains 0x0 on all lanes
   - TIP does not progress to WRLVL phase

---

## Failure Analysis

### Current Failure Point

**Symptom:**
- After manual training completes, TIP does not progress from BCLK_SCLK phase to WRLVL phase
- `training_status` = 0x1 (only BCLK_SCLK bit set)
- All lanes show `wl_delay = 0x0` (WRLVL not started)
- `gt_state = 0xB` (normal, not an error state)

**Timing:**
- Manual training completes successfully
- State machine simulation executes
- MR2 WRLVL enabled (0xAD)
- TIP remains stuck at BCLK_SCLK phase

### Root Cause Hypotheses

#### Hypothesis 1: TIP Needs State Machine Acknowledgment
**Theory:** TIP may require the software state machine to explicitly acknowledge ADDCMD skip before it can start WRLVL.

**Evidence:**
- HSS uses a state machine that transitions: ADDCMD state → checks skip → WRLVL state
- Our code polls `training_status` but doesn't simulate state transitions
- TIP might be waiting for state machine to be in WRLVL state

**Status:** ✅ Implemented state machine simulation, but still stuck

#### Hypothesis 2: MR2 WRLVL Enable Timing
**Theory:** MR2 WRLVL must be enabled at a specific time relative to TIP state transitions.

**Evidence:**
- HSS doesn't explicitly enable MR2 WRLVL in code (TIP may do it automatically)
- We enable MR2 WRLVL manually after state simulation
- Timing might be wrong

**Status:** ⚠️ Timing adjusted, but still stuck

#### Hypothesis 3: TIP Needs Explicit Signal
**Theory:** TIP requires a specific register write or signal to transition from ADDCMD (skipped) to WRLVL.

**Evidence:**
- No explicit register found that triggers WRLVL start
- `training_start` register exists but may need specific sequence
- DFI signals might need toggling

**Status:** ❓ Unknown - needs investigation

#### Hypothesis 4: Missing Configuration
**Theory:** Some register or configuration is missing that TIP needs to start WRLVL.

**Evidence:**
- All known registers match HSS values
- DPC_BITS, ODT, training_skip all correct
- IBUFMD registers set correctly

**Status:** ❓ Unknown - needs deeper investigation

---

## What We've Tried

### Attempt 1: Correct MR Values
- Updated LPDDR4 Mode Register values to match Libero config
- Result: ❌ No change - still stuck

### Attempt 2: Manual Write Leveling
- Implemented manual DFI write leveling using expert mode
- Result: ❌ Removed - conflicts with TIP automatic training

### Attempt 3: TIP Restart Sequence
- Added TIP restart after manual training (training_start toggle, DFI init toggle)
- Result: ❌ No change - still stuck

### Attempt 4: WRLVL Configuration Before Reset
- Moved WRLVL config (vrgen_h, ODT) before training reset release
- Result: ✅ Correct sequence, but still stuck

### Attempt 5: IBUFMD Initialization
- Added LPDDR4 IBUFMD register initialization
- Result: ✅ Correct, but still stuck

### Attempt 6: MR2 WRLVL Enable Before TIP
- Enable MR2 WRLVL after manual training, before waiting for TIP
- Result: ✅ Correct sequence, but still stuck

### Attempt 7: State Machine Simulation (Current)
- Simulate HSS state machine transitions (BCLKSCLK → ADDCMD → WRLVL)
- Add delays for state transitions
- Result: ❌ Still stuck - TIP doesn't progress

---

## Key Findings from HSS Analysis

### 1. TIP Runs Automatically
- No explicit restart needed after manual training
- TIP continues automatically from where it left off
- State machine just polls `training_status` - doesn't control TIP

### 2. Timing is Critical
- HSS checks `training_status` immediately after manual training
- By POST_MANUAL_TRAINING check, TIP has already completed all phases
- Suggests TIP runs very quickly once conditions are met

### 3. gt_state=0xB is Normal
- Successful HSS training shows `gt_state=0xB` throughout
- This is NOT an error state
- Don't use gt_state to detect completion

### 4. Write Leveling Delays Populate Automatically
- `wl_delay` values appear automatically when WRLVL completes
- No manual intervention needed
- Primary indicator of WRLVL completion

### 5. MR2 WRLVL Enable
- HSS code doesn't explicitly write MR2 with WRLVL enabled
- TIP likely enables it automatically when starting WRLVL phase
- Or controller enables it based on TIP state

### 6. State Machine is Polling, Not Controlling
- HSS state machine polls `training_status` bits
- It doesn't send commands to TIP
- TIP runs independently based on hardware conditions

---

## Register Values Comparison

### Successful HSS Training (from logs)
```
training_status: 0x1D (all phases complete)
training_skip: 0x02 (skip ADDCMD)
training_reset: 0x0 (released)
DPC_BITS: 0x47452 (after manual training)
rpc3_ODT: 0x3 (after training)
gt_state: 0xB (normal)
wl_delay: 0x19, 0x18, 0x1A, 0x19, 0x1F (lanes 0-4)
```

### Our Current State
```
training_status: 0x1 (BCLK_SCLK only)
training_skip: 0x02 (skip ADDCMD) ✅
training_reset: 0x0 (released) ✅
DPC_BITS: 0x50452 (vrgen_h=0x5) ✅
rpc3_ODT: 0x0 (during WRLVL) ✅
gt_state: 0xB (normal) ✅
wl_delay: 0x0, 0x0, 0x0, 0x0, 0x0 ❌ (not started)
```

**Difference:** DPC_BITS value (0x47452 vs 0x50452) - need to investigate what this means.

---

## Next Steps / Options to Try

### Option A: Investigate DPC_BITS Difference
- HSS shows `DPC_BITS: 0x47452` after manual training
- We have `DPC_BITS: 0x50452`
- Difference: bits 9:4 (vrgen_h) - HSS might restore different value
- **Action:** Check if DPC_BITS needs to be restored before TIP starts WRLVL

### Option B: Check Controller State
- Verify `DFI_INIT_COMPLETE` remains set
- Check if controller needs to be in specific state
- **Action:** Add debug output for controller state registers

### Option C: TIP Internal State Machine
- TIP might have internal state that needs specific conditions
- May need to check TIP-specific registers not documented
- **Action:** Search register map for TIP state/control registers

### Option D: Timing Adjustments
- Increase delays after state machine simulation
- Add delay after MR2 WRLVL enable
- **Action:** Try longer delays (1ms, 10ms) to see if TIP needs more time

### Option E: Remove Manual MR2 WRLVL Enable
- Let TIP enable MR2 WRLVL automatically
- Remove our MR2 WRLVL enable code
- **Action:** Test if TIP handles MR2 automatically

### Option F: DFI Signal Toggling
- Toggle `DFI_INIT_START` again after manual training
- May trigger TIP to continue
- **Action:** Try DFI init restart sequence

### Option G: Training Start Register Sequence
- Toggle `training_start` register in specific sequence
- May need to clear then set
- **Action:** Try training_start = 0, delay, training_start = 1

### Option H: Contact Microchip Support
- This may be a known issue or require specific sequence
- Register map or documentation might be incomplete
- **Action:** Open support case with Microchip

---

## Code Locations

### Main Training Function
- **File:** `hal/mpfs250.c`
- **Function:** `run_training()`
- **Line:** ~912

### Key Sections:
1. **WRLVL Configuration:** Lines ~1028-1038
2. **Training Reset Release:** Lines ~1040-1045
3. **DFI Init:** Lines ~1047-1057
4. **Manual LPDDR4 Training:** Lines ~1068-1690
5. **State Machine Simulation:** Lines ~1692-1730
6. **TIP Wait Loop:** Lines ~1732-1820

### Register Definitions
- **File:** `hal/mpfs250.h`
- **PHY Registers:** Lines ~400-500
- **Controller Registers:** Lines ~200-300

---

## References

### Documentation
1. **PolarFire Family Memory Controller User Guide VB**
   - Path: `/home/davidgarske/Projects/TwoSixTech/PolarFire_FPGA_PolarFire_SoC_FPGA_Memory_Controller_User_Guide_VB.txt`
   - Sections: 2.7.3.4 (Write Leveling), 3.4 (Training Sequence)

2. **PolarFire SoC MSS Technical Reference Manual VC**
   - Path: `/home/davidgarske/Projects/TwoSixTech/PolarFire_SoC_FPGA_MSS_Technical_Reference_Manual_VC.txt`
   - Section: 3.11 (MSS DDR Controller)

3. **Microchip Online Docs - LPDDR4 Troubleshooting**
   - URL: https://onlinedocs.microchip.com/oxy/GUID-7F276F66-9418-456E-9FA3-8E7EE40C9E25-en-US-7/GUID-3C46F146-3BCB-4847-8790-263398D5F223.html

### Reference Code
1. **HSS (Hart Software Services)**
   - Path: `/home/davidgarske/GitHub/hart-software-services/`
   - File: `baremetal/polarfire-soc-bare-metal-library/src/platform/mpfs_hal/common/nwc/mss_ddr.c`
   - Function: `lpddr4_manual_training()` (line ~5102)
   - Function: `ddr_setup()` state machine (line ~348)

2. **Successful HSS Training Log**
   - Path: `/home/davidgarske/Projects/TwoSixTech/ddr_training.txt`
   - Shows complete training sequence with all phases

### Configuration Files
1. **Libero Configuration**
   - Path: `hal/mpfs250/MSS_VIDEO_KIT_H264.cfg`
   - Contains DDR controller and PHY settings

2. **Register Map**
   - Path: `/home/davidgarske/Projects/TwoSixTech/PolarFireSoC_Register_Map/`
   - HTML format register definitions

---

## Test Logs

```
=== E51 (hart 0) Output - MMUART0 ===
wolfBoot Version: 2.7.0 (Jan 12 2026 16:22:42)
Running on E51 (hart 0) in M-mode

========================================
MPFS DDR Init (Video Kit LPDDR4 2GB)
MT53D512M32D2DS-053 x32 @ 1600 Mbps
========================================
DDR: NWC init...
  MSSIO...done
  STARTUP=0x3F1F00 DYN_CNTL=0x4FF
  MSSIO_CR=0x3880
DDR: Configuring SGMII/clock mux...
  Soft reset CFM to load NV map...done
  RFCKMUX after NV load = 0x5
  CLK_XCVR=0x2C30
DDR: Configuring MSS PLL...
  Initial MSS PLL CTRL=0x800010C6
  Using external refclk (RFCKMUX=0x5)
  PLL_CKMUX=0x155
  BCLKMUX=0x208
  Powering up PLL (CTRL=0x100001F)...
  After power up: CTRL=0x3300001F
  Waiting for MSS PLL lock...locked (0x3300001F)�DDR: Configuring DDR PLL...
  DDR bank controller reset...done
  Waiting for DDR PLL lock...locked (0x3300003F)
DDR: Enable DDRC clock/reset...CLK before=0x21 after=0x800021
  RST=0x3F7FFFDE
  Test MC_BASE2@0x20084000: SR=0x0 RAS=0x1C
done
DDR: Blocker@0x20005D1C before=0x0 after=0x1
DDR: PHY setup...  PVT calib...done
PHY PLL locked
    SR before=0x1
    SR after 0=0x0
    SR after 1=0x1
DDR: After rotation SR_N=0x1
DDR: BCLK90 rotation...done
DDR: BCLK phase...0x5003
DDR: Starting TIP training...
  Configure PHY for WRLVL...DPC=0x50452 ODT=0x0...done
  Training reset release...done
  DFI init start...done
  Wait DFI complete...OK
  LPDDR4 manual training...
    Device reset...done
    PLL freq double...done
    Second reset...done
    Pre-MR: CKE=0 RST=0 CS=1 PLL=0x20010BE
    DIV0_1=0x4000200 DIV2_3=0x2070205
    MR writes...ack=80 err=0...done
    PLL freq restore...done
    CA VREF training...
0x10...done
    ADDCMD training...phase=6 dly=12...PLL_PHADJ=0x501B DPC=0x50452...    MR re-write...ack=80 err=0...done
    Post-manual training status:
      train_stat=0x1 dfi_train_complete=0x0
      gt_state=0xB dqdqs_state=0x8
    ZQ cal...done
    Simulate state machine transitions...BCLK_SCLK done ADDCMD skipped MR2 WRLVL enable...done
    Post-state-machine: train_stat=0x1
  Wait for TIP WRLVL to start and complete...
      Progress: train_stat=0x1 (iter=0)
    Training status: 0x1
    training_skip=0x2 training_reset=0x0
    Per-lane status:
      L0: gt_err=0x0 gt_state=0xB wl_dly=0x0 dqdqs_st=0x8
      L1: gt_err=0x0 gt_state=0xB wl_dly=0x0 dqdqs_st=0x8
      L2: gt_err=0x0 gt_state=0xB wl_dly=0x0 dqdqs_st=0x8
      L3: gt_err=0x0 gt_state=0xB wl_dly=0x0 dqdqs_st=0x8
      L4: gt_err=0x0 gt_state=0xB wl_dly=0x0 dqdqs_st=0x8
    TIP cfg: tip_cfg_params=0x7CFE02F
    BCLK: pll_phadj=0x501B bclk_sclk=0x0
    RPC: rpc145=0x12 rpc147=0x13 rpc156=0x6 rpc166=0x2
    TIP training timeout or incomplete
      all_lanes_trained=0 train_stat=0x1
  Restore ODT and disable WRLVL...done
  Final train_stat=0x1
Write calib...MTC timeout...MTC timeout...MTC timeout...MTC timeout...MTC timeout...MTC timeout...MTC timeout...
```

---

## Summary

The DDR training sequence is correctly implemented up to the point where TIP should automatically progress from BCLK_SCLK to WRLVL phase. All manual training steps complete successfully, and the configuration matches HSS reference code. However, TIP does not transition to WRLVL phase automatically.

**Key Insight:** HSS shows that TIP completes all phases automatically between manual training end and the POST check, suggesting TIP runs continuously once conditions are met. Our implementation may be missing a condition that TIP needs to detect before starting WRLVL.

**Most Likely Issue:** TIP may need to see a specific state or signal that we're not providing, or there may be a timing/sequencing issue that prevents TIP from detecting it's ready to start WRLVL.

**Recommended Next Step:** Try Option E (remove manual MR2 WRLVL enable) to see if TIP handles it automatically, or Option H (contact Microchip support) if this is a known issue.


*Last Updated: 2026-01-12*
*Status: Investigation ongoing - TIP stuck at BCLK_SCLK phase*
