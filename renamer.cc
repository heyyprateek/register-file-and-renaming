#include "renamer.h"
#include <assert.h>
#include <stdlib.h>
#include <numeric>
#include <iostream>

void initializeBranchCheckpoints(BranchCPEntry* BranchCheckpoints, uint64_t n_branches, uint64_t n_phys_regs){
    for (int i = 0; i < n_branches; i++) {
        BranchCheckpoints[i].ShadowMapTab = new uint64_t[n_phys_regs];
        BranchCheckpoints[i].CheckPointFL = new uint64_t[n_phys_regs];
        BranchCheckpoints[i].CheckPointGBM = 0;
    }
}

void initializeAL(ALEntry* AL, uint64_t n_phys_regs){
    for (int i = 0; i < n_phys_regs; i++)
    {
        AL[i].has_dest = true;
        AL[i].dest_log_reg = 0;
        AL[i].dest_phys_reg = 0;
        AL[i].comp = false;
        AL[i].exc = false;
        AL[i].ldvltn = false;
        AL[i].brmispred = false;
        AL[i].valmispred = false;
        AL[i].isload = false;
        AL[i].isstore = false;
        AL[i].isbranch = false;
        AL[i].isamo = false;
        AL[i].iscsr = false;
        AL[i].pc = 0;
    }
}

renamer::renamer(uint64_t n_log_regs,
		         uint64_t n_phys_regs,
		         uint64_t n_branches,
		         uint64_t n_active)
{
    this->n_log_regs = n_log_regs;
    this->n_phys_regs = n_phys_regs;
    this->n_branches = n_branches;
    this->n_active = n_active;

    // Assertions
    assert(n_phys_regs > n_log_regs);
    assert(n_branches >= 1 && n_branches <= 64);
    assert(n_active > 0);

    // Allocate space for data structures
    // Structure 1: Rename Map Table
    RMT = new uint64_t[n_log_regs];
    std::iota(RMT, RMT + n_log_regs, 0); // fills the array with values same as the index

    // Structure 2: Architectural Map Table
    AMT = new uint64_t[n_log_regs];
    std::iota(AMT, AMT + n_log_regs, 0); // fills the array with values same as the index

    // Structure 3: Free List
    FL = new uint64_t[n_phys_regs];
    std::iota(FL, FL + n_phys_regs, 0); // fills the array with values same as the index
    uint64_t* flHead = FL;
    uint64_t* flTail = FL;
    // Mark FL full initially
    flTailPhase = 1;
    flHeadPhase = !flTailPhase;

    // Structure 4: Active List
    AL = new ALEntry[n_phys_regs];
    initializeAL(AL, n_phys_regs);
    uint64_t* alHead = AL;
    uint64_t* alTail = AL;
    alTailPhase = alHeadPhase = 0;

    // Structure 5: Physical Register File
    PRF = new uint64_t[n_phys_regs];

    // Structure 6: Physical Register File Ready Bit Array
    PRFRdyBits = new bool[n_phys_regs];

    // Structure 7: Global Branch Mask (GBM)
    GBM = 0;

    // Structure 8: Branch Checkpoints
    BranchCheckpoints = new BranchCPEntry[n_branches];
    initializeBranchCheckpoints(BranchCheckpoints, n_branches, n_phys_regs);
}


