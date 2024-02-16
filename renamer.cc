#include "renamer.h"
#include <assert.h>
#include <stdlib.h>
#include <numeric>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdarg> // Include the cstdarg header for variable argument handling

using namespace std;

# define DEBUG 0
void debugPrint(const char* format, ...) {
   #if DEBUG
      va_list args;
      va_start(args, format);
      vprintf(format, args);
      va_end(args);
   #endif
}

void copyArray(uint64_t* source, uint64_t* destination, uint64_t size) {
    // memcpy(destination, source, size * sizeof(int));
    for (uint64_t i = 0; i < size; ++i) {
        destination[i] = source[i];
    }
}

void renamer::initializeBranchCheckpoints(BranchCPEntry *BranchCheckpoints, 
                                          uint64_t n_branches, 
                                          const uint64_t *RMT, 
                                          uint64_t n_log_regs, 
                                          uint64_t GBM) {

    for (int i = 0; i < n_branches; i++) {
        BranchCheckpoints[i].ShadowMapTab = new uint64_t[n_log_regs];
    }
    for (int j = 0; j < n_branches; j++) {
        for (uint64_t k = 0; k < n_log_regs; k++) {
            BranchCheckpoints[j].ShadowMapTab[k] = RMT[k];
        }
        BranchCheckpoints[j].flHead = 0;
        BranchCheckpoints[j].flHeadPhase = 0;
        // BranchCheckpoints[j].GBM = GBM;
    }
}

void renamer::fillFL(uint64_t *FL, uint64_t n_log_regs, uint64_t n_phys_regs, const uint64_t *RMT) {
    // Allocate memory for temporary array 'temp'
    uint64_t *temp = new uint64_t[n_phys_regs];

    // Initialize array 'a' with zeros
    for (uint64_t i = 0; i < n_phys_regs; i++) {
        temp[i] = 0;
    }

    
    // Mark values in 'temp' which are present in 'RMT'
    for(uint64_t i = 0; i < (n_phys_regs); i++){
    for (uint64_t j = 0; j < n_log_regs; j++) {
        if (RMT[j] == i) {
            temp[RMT[j]] = 1;
        }
    }
    }
    // Copy values to Fl based on 'temp'
    uint64_t k = 0;
    for (uint64_t i = 0; i < n_phys_regs; i++) {
        if (temp[i] == 0) {
            FL[k] = i;
            k++;
        }
    }

    // Deallocate memory for array 'temp'
    delete[] temp;
}

void renamer::resetALEntry(ALEntry* AL, uint64_t index) {
    AL[index].has_dest = false;
    AL[index].dest_log_reg = 0;
    AL[index].dest_phys_reg = 0;
    AL[index].comp = false;
    AL[index].exc = false;
    AL[index].ldvltn = false;
    AL[index].brmispred = false;
    AL[index].valmispred = false;
    AL[index].isload = false;
    AL[index].isstore = false;
    AL[index].isbranch = false;
    AL[index].isamo = false;
    AL[index].iscsr = false;
    AL[index].pc = 0;
}

void renamer::initializeAL(ALEntry* AL, uint64_t n_active) {
    for (int i = 0; i < n_active; i++)
    {
        resetALEntry(AL, i);
    }
}

renamer::renamer(uint64_t n_log_regs,
		         uint64_t n_phys_regs,
		         uint64_t n_branches,
		         uint64_t n_active)
{
    debugPrint("constructor \n");
    // Assertions
    assert(n_phys_regs > n_log_regs);
    assert(n_branches >= 1 && n_branches <= 64);
    assert(n_active > 0);

    this->n_log_regs  = n_log_regs;
    this->n_phys_regs = n_phys_regs;
    this->n_branches  = n_branches;
    this->n_active    = n_active;

    debugPrint("n_log_regs %llu, n_phys_regs %llu, n_branches %llu, n_active %llu\n", n_log_regs, n_phys_regs, n_branches, n_active);

    // Allocate space for data structures
    // Structure 1: Rename Map Table
    RMT = new uint64_t[n_log_regs];
    std::iota(RMT, RMT + n_log_regs, 0); // fills the array with values same as the index

    // Structure 2: Architectural Map Table
    AMT = new uint64_t[n_log_regs];
    std::iota(AMT, AMT + n_log_regs, 0); // fills the array with values same as the index

    // Structure 3: Free List
    flSize = n_phys_regs - n_log_regs;
    FL = new uint64_t[flSize];
    fillFL(FL, n_log_regs, n_phys_regs, RMT); // fills the array with values same as the index
    flHead = flTail = 0;
    // Mark FL full initially
    flTailPhase = true;
    flHeadPhase = !flTailPhase;

    // Structure 4: Active List
    AL = new ALEntry[n_active];
    initializeAL(AL, n_active);
    alHead = alTail = 0;
    alTailPhase = alHeadPhase = false;

    // Structure 5: Physical Register File
    PRF = new uint64_t[n_phys_regs];
    // std::iota(PRF, PRF + n_phys_regs, 0);

    // Structure 6: Physical Register File Ready Bit Array
    PRFRdyBits = new bool[n_phys_regs];
    for (int i = 0; i < n_phys_regs; i++) {
        if (i < n_log_regs) {
            PRFRdyBits[i] = true;
        }
        else {
            PRFRdyBits[i] = false;
        }
    }

    // Structure 7: Global Branch Mask (GBM)
    GBM = 0;

    // Structure 8: Branch Checkpoints
    BranchCheckpoints = new BranchCPEntry[n_branches];
    initializeBranchCheckpoints(BranchCheckpoints, n_branches, RMT, n_log_regs, GBM);
}

renamer::~renamer() {
    // Free dynamically allocated memory
    delete[] RMT;
    delete[] AMT;
    delete[] FL;
    delete[] AL;
    delete[] PRF;
    delete[] PRFRdyBits;
    delete[] BranchCheckpoints;
}

//////////////////////////////////////////
// Functions related to Rename Stage.   //
//////////////////////////////////////////
bool renamer::stall_reg(uint64_t bundle_dst) {
    debugPrint("entering stall_reg\n");
   
    if (flHeadPhase != flTailPhase) { //head ahead of the tail; full size - (head - tail) tells the no. of available entries
        debugPrint("head phase not same as tail phase\n");
        return (flSize - (flHead - flTail) < bundle_dst);
    }
    else { //tail ahead of the head; tail - head tells the no. of available entries
        debugPrint("head phase same as tail phase\n");
        return (flTail - flHead < bundle_dst);
    }
}

bool renamer::stall_branch(uint64_t bundle_branch) {
    debugPrint("entering stall_branch\n");
    int avlZeroCount = 0;
    debugPrint("avl %llu, GBM %llu, n_branches %llu\n", avlZeroCount, GBM, n_branches);
    for (uint64_t i = 0; i < n_branches; i++) {
        if((GBM & (1ULL << i)) == 0) {
            avlZeroCount++;
            debugPrint("===%llu\n", avlZeroCount);
        }
    }
    debugPrint("---- %llu\n", avlZeroCount);
    return (avlZeroCount < bundle_branch);
}

uint64_t renamer::get_branch_mask() {
    debugPrint("getting GBM\n");
    return GBM;
}

uint64_t renamer::rename_rsrc(uint64_t log_reg)
{
    debugPrint("entering rename rsrc\n");
    return RMT[log_reg];
}

uint64_t renamer::rename_rdst(uint64_t log_reg)
{
    debugPrint("entering rename rdst\n");
    uint64_t phyRegAtFLHead = FL[flHead];
    flHead++;
    if (flHead == flSize) {
        flHead = 0;
        flHeadPhase = !flHeadPhase;
    }
    // update RMT
    RMT[log_reg] = phyRegAtFLHead;
    return phyRegAtFLHead;
}

uint64_t renamer::checkpoint() {
    debugPrint("entering checkpoint\n");
    uint64_t freeBranchPos = 0;
    while (((GBM & (1ULL << freeBranchPos)) != 0) && (freeBranchPos < n_branches)) {
        freeBranchPos++;
    }

    // Update GBM
    GBM |= (1ULL << freeBranchPos);

    // Create checkpoint
    debugPrint("checkpoint copy array above\n");
    copyArray(RMT, BranchCheckpoints[freeBranchPos].ShadowMapTab, n_log_regs);
    debugPrint("checkpoint copy array below\n");
    BranchCheckpoints[freeBranchPos].flHead = flHead;
    BranchCheckpoints[freeBranchPos].flHeadPhase = flHeadPhase;
    BranchCheckpoints[freeBranchPos].GBM = GBM;

    return freeBranchPos;
}

//////////////////////////////////////////
// Functions related to Dispatch Stage. //
//////////////////////////////////////////


bool renamer::stall_dispatch(uint64_t bundle_inst)
{
    debugPrint("entering stall dispatch\n");
    uint64_t n_occ_al_entries = (alTailPhase == alHeadPhase) ? (n_active - (alTail - alHead)) : (alHead - alTail);
    return (n_occ_al_entries) < bundle_inst;
}

uint64_t  renamer::dispatch_inst(bool dest_valid,
                        uint64_t log_reg,
                        uint64_t phys_reg,
                        bool load,
                        bool store,
                        bool branch,
                        bool amo,
                        bool csr,
                        uint64_t PC) {
    
    debugPrint("entering dispatch inst\n");
    assert(!(alHeadPhase != alTailPhase && alHead == alTail));
    uint64_t currentALTail = alTail;
    AL[alTail].has_dest = dest_valid;
    if (dest_valid) {
        AL[alTail].dest_log_reg = log_reg;
        AL[alTail].dest_phys_reg = phys_reg;
    }
    AL[alTail].comp = false;
    AL[alTail].exc = false;
    AL[alTail].ldvltn = false;
    AL[alTail].brmispred = false;
    AL[alTail].valmispred = false;
    AL[alTail].isload = load;
    AL[alTail].isstore = store;
    AL[alTail].isbranch = branch;
    AL[alTail].isamo = amo;
    AL[alTail].iscsr = csr;
    AL[alTail].pc = PC;

    // Advance tail
    alTail++;
    if (alTail == n_active) { // wrap around scenario
        alTail = 0; // force wrap around
        alTailPhase = !alTailPhase; // toggle tail phase bit
    }

    return currentALTail;
}

bool renamer::is_ready(uint64_t phys_reg)
{
    debugPrint("entering is ready\n");
    return (PRFRdyBits[phys_reg] == 1);
}

void renamer::clear_ready(uint64_t phys_reg)
{
    debugPrint("entering clear ready\n");
    PRFRdyBits[phys_reg] = false;
}

//////////////////////////////////////////
// Functions related to the Reg. Read   //
// and Execute Stages.                  //
//////////////////////////////////////////


uint64_t renamer::read(uint64_t phys_reg) 
{
    debugPrint("entering read\n");
    return PRF[phys_reg];
}

void renamer::set_ready(uint64_t phys_reg)
{
    debugPrint("entering set ready\n");
    PRFRdyBits[phys_reg] = true;
}

//////////////////////////////////////////
// Functions related to Writeback Stage.//
//////////////////////////////////////////
void renamer::write(uint64_t phys_reg, uint64_t value) {
    debugPrint("entering write\n");
    PRF[phys_reg] = value;
}

void renamer::set_complete(uint64_t AL_index) {
    debugPrint("entering set complete\n");
    AL[AL_index].comp = true;
}

void renamer::resolve(uint64_t AL_index,
                      uint64_t branch_ID,
                      bool correct) {
    
    debugPrint("entering resolve\n");
    if (!correct) {// Misprediction
        uint64_t mask = (1ULL << branch_ID) ^ UINT64_MAX;
        GBM = BranchCheckpoints[branch_ID].GBM & mask;
        copyArray(BranchCheckpoints[branch_ID].ShadowMapTab, RMT, n_log_regs);
        flHead = BranchCheckpoints[branch_ID].flHead;
        flHeadPhase = BranchCheckpoints[branch_ID].flHeadPhase;
        if (((AL_index + 1) % n_active) > alTail) {
            alTailPhase = !alTailPhase;
        }
        alTail = (AL_index + 1) % n_active;
    }
    else {// Correct prediction
        GBM &= ~(1ULL << branch_ID);
        for (uint64_t i = 0; i < n_branches; i++) {
            BranchCheckpoints[i].GBM &= ~(1ULL << branch_ID);
        }
    }

}

//////////////////////////////////////////
// Functions related to Retire Stage.   //
//////////////////////////////////////////

bool renamer::precommit(bool &completed,
                        bool &exception, 
                        bool &load_viol, 
                        bool &br_misp, 
                        bool &val_misp,
                        bool &load, 
                        bool &store, 
                        bool &branch, 
                        bool &amo, 
                        bool &csr,
                        uint64_t &PC) {
    debugPrint("entering precommit\n");
    if (alTail == alHead && alHeadPhase == alTailPhase) return false;
    else {
        completed = AL[alHead].comp;
        exception = AL[alHead].exc;
        load_viol = AL[alHead].ldvltn;
        br_misp = AL[alHead].brmispred;
        val_misp = AL[alHead].valmispred;
        load = AL[alHead].isload;
        store = AL[alHead].isstore;
        branch =  AL[alHead].isbranch;
        amo =  AL[alHead].isamo;
        csr =  AL[alHead].iscsr;
        PC =  AL[alHead].pc;
        return true;
    }
}

void renamer::commit() {
    assert(!(alTail == alHead && alHeadPhase == alTailPhase));
    assert(AL[alHead].comp == true);
    assert(AL[alHead].exc == false);
    assert(AL[alHead].ldvltn == false);
    debugPrint("entering commit\n");

    if (AL[alHead].has_dest) { // retiring instruction had a valid destination register specifier
        // Free the physical register present in the AMT at retiring instruction's destination logical register specifier
        uint64_t ret_dest_log_reg = AL[alHead].dest_log_reg;
        FL[flTail] = AMT[ret_dest_log_reg];
        flTail++;
        if (flTail == flSize) {
            flTail = 0;
            flTailPhase = !flTailPhase;
        }

        // Update the AMT
        AMT[ret_dest_log_reg] = AL[alHead].dest_phys_reg;

    }

    // Retire the instruction at the head and advance the head pointer
    resetALEntry(AL, alHead);
    alHead++;
    if (alHead == n_active) {
        alHead = 0;
        alHeadPhase = !alHeadPhase;
    }
}

void renamer::squash() {
    debugPrint("entering squash\n");
    // restore RMT from AMT
    copyArray(AMT, RMT, n_log_regs);

    // restore FL
    flHead = flTail;
    flTailPhase = true;
    flHeadPhase = !flTailPhase;

    // squash instructions in the Active List
    alTail = alHead;
    alTailPhase = alHeadPhase = false;

    // reset GBM;
    GBM = 0;
}

/////////////////////////////////////////////////////////////////////
// Functions for individually setting the exception bit,
// load violation bit, branch misprediction bit, and
// value misprediction bit, of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
void renamer::set_exception(uint64_t AL_index) {
    debugPrint("entering set exception\n");
    AL[AL_index].exc = true;
}
void renamer::set_load_violation(uint64_t AL_index) {
    debugPrint("entering set load violation\n");
    AL[AL_index].ldvltn = true;
}
void renamer::set_branch_misprediction(uint64_t AL_index) {
    debugPrint("entering branch misprediction\n");
    AL[AL_index].brmispred = true;
}
void renamer::set_value_misprediction(uint64_t AL_index) {
    debugPrint("entering set value misprediction\n");
    AL[AL_index].valmispred = true;
}

/////////////////////////////////////////////////////////////////////
// Query the exception bit of the indicated entry in the Active List.
/////////////////////////////////////////////////////////////////////
bool renamer::get_exception(uint64_t AL_index) {
    debugPrint("entering get exception\n");
    return AL[AL_index].exc;
}
