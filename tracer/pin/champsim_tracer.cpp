
/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs 
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <string.h>
#include <string>

#define NUM_INSTR_DESTINATIONS 2
#define NUM_INSTR_SOURCES 4

typedef struct trace_instr_format {
    unsigned long long int ip = 0;  // instruction pointer (program counter) value

    unsigned char is_branch = 0;    // is this branch
    unsigned char branch_taken = 0; // if so, is this taken

    unsigned char destination_registers[NUM_INSTR_DESTINATIONS] = {}; // output registers
    unsigned char source_registers[NUM_INSTR_SOURCES] = {};           // input registers

    unsigned long long int destination_memory[NUM_INSTR_DESTINATIONS] = {}; // output memory
    unsigned long long int source_memory[NUM_INSTR_SOURCES] = {};           // input memory
} trace_instr_format_t;

/* ================================================================== */
// Global variables 
/* ================================================================== */

UINT64 instrCount = 0;

FILE* out;

trace_instr_format_t curr_instr;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<std::string> KnobOutputFile(KNOB_MODE_WRITEONCE,  "pintool", "o", "champsim.trace", 
        "specify file name for Champsim tracer output");

KNOB<UINT64> KnobSkipInstructions(KNOB_MODE_WRITEONCE, "pintool", "s", "0", 
        "How many instructions to skip before tracing begins");

KNOB<UINT64> KnobTraceInstructions(KNOB_MODE_WRITEONCE, "pintool", "t", "1000000", 
        "How many instructions to trace");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage()
{
  std::cerr << "This tool creates a register and memory access trace" << std::endl 
        << "Specify the output trace file with -o" << std::endl 
        << "Specify the number of instructions to skip before tracing with -s" << std::endl
        << "Specify the number of instructions to trace with -t" << std::endl << std::endl;

  std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

void ResetCurrentInstruction(VOID *ip)
{
    curr_instr = {};
    curr_instr.ip = (unsigned long long int)ip;
}

BOOL ShouldWrite()
{
  ++instrCount;
  return (instrCount > KnobSkipInstructions.Value()) && (instrCount <= (KnobTraceInstructions.Value()+KnobSkipInstructions.Value()));
}

void WriteCurrentInstruction()
{
  fwrite(&curr_instr, sizeof(trace_instr_format_t), 1, out);
}

void BranchOrNot(UINT32 taken)
{
    curr_instr.is_branch = 1;
    curr_instr.branch_taken = taken;
}

void RegInstrument(VOID* begin, VOID* end, UINT32 r)
{
    // check to see if this register is already in the list
    auto found_reg = std::find((unsigned char*)begin, (unsigned char*)end, r);
    if (found_reg == (unsigned char*)end) {
      found_reg = std::find((unsigned char*)begin, (unsigned char*)end, 0);
      *found_reg = r;
    }
}

void MemInstrument(VOID* begin, VOID* end, ADDRINT addr)
{
    // check to see if this memory read location is already in the list
    auto found_reg = std::find((unsigned long long int *)begin, (unsigned long long int *)end, addr);
    if (found_reg == end) {
      found_reg = std::find((unsigned long long int *)begin, (unsigned long long int *)end, 0);
      *found_reg = addr;
    }
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v)
{
    // begin each instruction with this function
    INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)ResetCurrentInstruction, IARG_INST_PTR, IARG_END);

    // instrument branch instructions
    if(INS_IsBranch(ins))
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)BranchOrNot, IARG_BRANCH_TAKEN, IARG_END);

    // instrument register reads
    UINT32 readRegCount = INS_MaxNumRRegs(ins);
    for(UINT32 i=0; i<readRegCount; i++) 
    {
        UINT32 regNum = INS_RegR(ins, i);
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RegInstrument,
            IARG_PTR, curr_instr.source_registers, IARG_PTR, curr_instr.source_registers + NUM_INSTR_SOURCES,
            IARG_UINT32, regNum, IARG_END);
    }

    // instrument register writes
    UINT32 writeRegCount = INS_MaxNumWRegs(ins);
    for(UINT32 i=0; i<writeRegCount; i++) 
    {
        UINT32 regNum = INS_RegW(ins, i);
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RegInstrument,
            IARG_PTR, curr_instr.destination_registers, IARG_PTR, curr_instr.destination_registers + NUM_INSTR_DESTINATIONS,
            IARG_UINT32, regNum, IARG_END);
    }

    // instrument memory reads and writes
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++) 
    {
        if (INS_MemoryOperandIsRead(ins, memOp)) 
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemInstrument,
                IARG_PTR, curr_instr.source_memory, IARG_PTR, curr_instr.source_memory + NUM_INSTR_SOURCES,
                IARG_MEMORYOP_EA, memOp, IARG_END);
        if (INS_MemoryOperandIsWritten(ins, memOp)) 
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MemInstrument,
                IARG_PTR, curr_instr.destination_memory, IARG_PTR, curr_instr.destination_memory + NUM_INSTR_DESTINATIONS,
                IARG_MEMORYOP_EA, memOp, IARG_END);
    }

    // finalize each instruction with this function
    INS_InsertIfCall(ins, IPOINT_BEFORE, (AFUNPTR)ShouldWrite, IARG_END);
    INS_InsertThenCall(ins, IPOINT_BEFORE, (AFUNPTR)WriteCurrentInstruction, IARG_END);
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID *v)
{
  fclose(out);
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char *argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid 
    if( PIN_Init(argc,argv) )
        return Usage();

    const char* fileName = KnobOutputFile.Value().c_str();

    out = fopen(fileName, "ab");
    if (!out) 
    {
      std::cout << "Couldn't open output trace file. Exiting." << std::endl;
        exit(1);
    }

    // Register function to be called to instrument instructions
    INS_AddInstrumentFunction(Instruction, 0);

    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
