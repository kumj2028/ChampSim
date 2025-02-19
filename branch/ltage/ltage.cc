#include "ooo_cpu.h"
#include "tage.h"
#include "loop_pred.h"

Tage tage_predictor[NUM_CPUS];                      // tage predictor component for each cpu 
LoopPred loop_predictor[NUM_CPUS];                  // loop predictor component for each cpu

int loop_correct[NUM_CPUS] = {0};                   // A counter to decide whose prediction to use
uint8_t tage_pred[NUM_CPUS], loop_pred[NUM_CPUS];   // The predictions of tage and loop predictor

// Updates the counter based on taken/not-taken
void ctr_update (uint8_t taken, uint32_t cpu) {
    if (taken == loop_pred[cpu]) {
        if (loop_correct[cpu] < 127) loop_correct[cpu]++;
    }
    else if (loop_correct[cpu] > -126) loop_correct[cpu]--;
}

// Initialises the predictor
void O3_CPU::initialize_branch_predictor()
{
    tage_predictor[cpu].init();
    loop_predictor[cpu].init();
}

// Predicts whether branch is taken or not
uint8_t O3_CPU::predict_branch(uint64_t ip, uint64_t predicted_target, uint8_t always_taken, uint8_t branch_type)
{
    loop_pred[cpu] = loop_predictor[cpu].get_pred(ip);
    tage_pred[cpu] = tage_predictor[cpu].predict(ip);
    if ((loop_predictor[cpu].is_valid) && (loop_correct[cpu] >= 0)) 
        return loop_pred[cpu];
    return tage_pred[cpu];
}

// Updates the predictor based on the last branch result
void O3_CPU::last_branch_result(uint64_t ip, uint64_t branch_target, uint8_t taken, uint8_t branch_type)
{
    tage_predictor[cpu].update(ip, taken);
    loop_predictor[cpu].update_entry(taken, tage_pred[cpu]);
    if (loop_predictor[cpu].is_valid && (tage_pred[cpu] != loop_pred[cpu])) ctr_update(taken, cpu);
}