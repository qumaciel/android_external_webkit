/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef DFGNonSpeculativeJIT_h
#define DFGNonSpeculativeJIT_h

#if ENABLE(DFG_JIT)

#include <dfg/DFGJITCodeGenerator.h>

namespace JSC { namespace DFG {

class SpeculationCheckIndexIterator;

// === EntryLocation ===
//
// This structure describes an entry point into the non-speculative
// code path. This is used in linking bail-outs from the speculative path.
struct EntryLocation {
    EntryLocation(MacroAssembler::Label, NonSpeculativeJIT*);

    // The node this entry point corresponds to, and the label
    // marking the start of code for the given node.
    MacroAssembler::Label m_entry;
    NodeIndex m_nodeIndex;

    // For every entry point we record a map recording for every
    // machine register which, if any, values it contains. For
    // GPR registers we must also record the format of the value.
    struct RegisterInfo {
        NodeIndex nodeIndex;
        DataFormat format;
    };
    RegisterInfo m_gprInfo[numberOfGPRs];
    NodeIndex m_fprInfo[numberOfFPRs];
};

// === NonSpeculativeJIT ===
//
// This class is used to generate code for the non-speculative path.
// Code generation will take advantage of static information available
// in the dataflow to perform safe optimizations - for example, avoiding
// boxing numeric values between arithmetic operations, but will not
// perform any unsafe optimizations that would render the code unable
// to produce the correct results for any possible input.
class NonSpeculativeJIT : public JITCodeGenerator {
    friend struct EntryLocation;
public:
    NonSpeculativeJIT(JITCompiler& jit)
        : JITCodeGenerator(jit, false)
    {
    }

    void compile(SpeculationCheckIndexIterator&);

    typedef SegmentedVector<EntryLocation, 16> EntryLocationVector;
    EntryLocationVector& entryLocations() { return m_entryLocations; }

private:
    void compile(SpeculationCheckIndexIterator&, Node&);
    void compile(SpeculationCheckIndexIterator&, BasicBlock&);

    bool isKnownInteger(NodeIndex);
    bool isKnownNumeric(NodeIndex);

    // These methods are used when generating 'unexpected'
    // calls out from JIT code to C++ helper routines -
    // they spill all live values to the appropriate
    // slots in the RegisterFile without changing any state
    // in the GenerationInfo.
    void silentSpillGPR(VirtualRegister spillMe, GPRReg exclude = InvalidGPRReg)
    {
        GenerationInfo& info = m_generationInfo[spillMe];
        ASSERT(info.registerFormat() != DataFormatNone && info.registerFormat() != DataFormatDouble);

        if (!info.needsSpill() || (info.gpr() == exclude))
            return;

        DataFormat registerFormat = info.registerFormat();
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(info.gpr());

        if (registerFormat == DataFormatInteger) {
            m_jit.orPtr(JITCompiler::tagTypeNumberRegister, reg);
            m_jit.storePtr(reg, JITCompiler::addressFor(spillMe));
        } else {
            ASSERT(registerFormat & DataFormatJS || registerFormat == DataFormatCell);
            m_jit.storePtr(reg, JITCompiler::addressFor(spillMe));
        }
    }
    void silentSpillFPR(VirtualRegister spillMe, GPRReg canTrample, FPRReg exclude = InvalidFPRReg)
    {
        GenerationInfo& info = m_generationInfo[spillMe];
        ASSERT(info.registerFormat() == DataFormatDouble);

        if (!info.needsSpill() || (info.fpr() == exclude))
            return;

        boxDouble(info.fpr(), canTrample);
        m_jit.storePtr(JITCompiler::gprToRegisterID(canTrample), JITCompiler::addressFor(spillMe));
    }

    void silentFillGPR(VirtualRegister spillMe, GPRReg exclude = InvalidGPRReg)
    {
        GenerationInfo& info = m_generationInfo[spillMe];
        if (info.gpr() == exclude)
            return;

        NodeIndex nodeIndex = info.nodeIndex();
        Node& node = m_jit.graph()[nodeIndex];
        ASSERT(info.registerFormat() != DataFormatNone && info.registerFormat() != DataFormatDouble);
        DataFormat registerFormat = info.registerFormat();
        JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(info.gpr());

        if (registerFormat == DataFormatInteger) {
            if (node.isConstant()) {
                ASSERT(isInt32Constant(nodeIndex));
                m_jit.move(Imm32(valueOfInt32Constant(nodeIndex)), reg);
            } else
                m_jit.load32(JITCompiler::addressFor(spillMe), reg);
            return;
        }

        if (node.isConstant())
            m_jit.move(constantAsJSValueAsImmPtr(nodeIndex), reg);
        else {
            ASSERT(registerFormat & DataFormatJS || registerFormat == DataFormatCell);
            m_jit.loadPtr(JITCompiler::addressFor(spillMe), reg);
        }
    }
    void silentFillFPR(VirtualRegister spillMe, GPRReg canTrample, FPRReg exclude = InvalidFPRReg)
    {
        GenerationInfo& info = m_generationInfo[spillMe];
        if (info.fpr() == exclude)
            return;

        NodeIndex nodeIndex = info.nodeIndex();
        Node& node = m_jit.graph()[nodeIndex];
        ASSERT(info.registerFormat() == DataFormatDouble);

        if (node.isConstant()) {
            JITCompiler::RegisterID reg = JITCompiler::gprToRegisterID(info.gpr());
            m_jit.move(constantAsJSValueAsImmPtr(nodeIndex), reg);
        } else {
            m_jit.loadPtr(JITCompiler::addressFor(spillMe), JITCompiler::gprToRegisterID(canTrample));
            unboxDouble(canTrample, info.fpr());
        }
    }

    void silentSpillAllRegisters(GPRReg exclude, GPRReg preserve = InvalidGPRReg)
    {
        GPRReg canTrample = (preserve == gpr0) ? gpr1 : gpr0;
        
        for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
            VirtualRegister name = m_gprs.name(gpr);
            if (name != InvalidVirtualRegister)
                silentSpillGPR(name, exclude);
        }
        for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
            VirtualRegister name = m_fprs.name(fpr);
            if (name != InvalidVirtualRegister)
                silentSpillFPR(name, canTrample);
        }
    }
    void silentSpillAllRegisters(FPRReg exclude, GPRReg preserve = InvalidGPRReg)
    {
        GPRReg canTrample = (preserve == gpr0) ? gpr1 : gpr0;
        
        for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
            VirtualRegister name = m_gprs.name(gpr);
            if (name != InvalidVirtualRegister)
                silentSpillGPR(name);
        }
        for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
            VirtualRegister name = m_fprs.name(fpr);
            if (name != InvalidVirtualRegister)
                silentSpillFPR(name, canTrample, exclude);
        }
    }
    void silentFillAllRegisters(GPRReg exclude)
    {
        GPRReg canTrample = (exclude == gpr0) ? gpr1 : gpr0;
        
        for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
            VirtualRegister name = m_fprs.name(fpr);
            if (name != InvalidVirtualRegister)
                silentFillFPR(name, canTrample);
        }
        for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
            VirtualRegister name = m_gprs.name(gpr);
            if (name != InvalidVirtualRegister)
                silentFillGPR(name, exclude);
        }
    }
    void silentFillAllRegisters(FPRReg exclude)
    {
        GPRReg canTrample = gpr0;
        
        for (FPRReg fpr = fpr0; fpr < numberOfFPRs; next(fpr)) {
            VirtualRegister name = m_fprs.name(fpr);
            if (name != InvalidVirtualRegister) {
#ifndef NDEBUG
                ASSERT(fpr != exclude);
#else
                UNUSED_PARAM(exclude);
#endif
                silentFillFPR(name, canTrample, exclude);
            }
        }
        for (GPRReg gpr = gpr0; gpr < numberOfGPRs; next(gpr)) {
            VirtualRegister name = m_gprs.name(gpr);
            if (name != InvalidVirtualRegister)
                silentFillGPR(name);
        }
    }

    // These methods are used to plant calls out to C++
    // helper routines to convert between types.
    void valueToNumber(JSValueOperand&, FPRReg result);
    void valueToInt32(JSValueOperand&, GPRReg result);
    void numberToInt32(FPRReg, GPRReg result);

    // Record an entry location into the non-speculative code path;
    // for every bail-out on the speculative path we record information
    // to be able to re-enter into the non-speculative one.
    void trackEntry(MacroAssembler::Label entry)
    {
        m_entryLocations.append(EntryLocation(entry, this));
    }

    EntryLocationVector m_entryLocations;
};

} } // namespace JSC::DFG

#endif
#endif

