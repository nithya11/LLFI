/***************
InstTrace.cpp
Author: Sam Coulter
  This llvm pass is part of the greater LLFI framework
  
  Run the pass with the opt -InstTrace option after loading LLFI.so
  
  This pass injects a function call before every non-void-returning, 
  non-phi-node instruction that prints trace information about the executed
  instruction to a file specified during the pass.
***************/

#include <vector>
#include <cmath>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/DataLayout.h"

#include "Utils.h"

using namespace llvm;

cl::opt<bool> debugtrace("debugtrace",
              cl::desc("Print tracing instrucmented instruction information"),
              cl::init(false));
cl::opt<int> maxtrace( "maxtrace",
    cl::desc("Maximum number of dynamic instructions that will be traced after fault injection"),
            cl::init(1000));

namespace llfi {

struct InstTrace : public FunctionPass {

  static char ID;

  InstTrace() : FunctionPass(ID) {}

  //Add AnalysisUsage Pass as prerequisite for InstTrace Pass
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<DataLayout>();
  }

  virtual bool doInitialization(Module &M) {
    return false;
  }

  virtual bool doFinalization(Module &M) {
    //Dont forget to delete the output filename string!
    Function* mainfunc = M.getFunction("main");
    if (mainfunc == NULL) {
      errs() << "ERROR: Function main does not exist, " <<
          "which is required by LLFI\n";
      exit(1);
    }

    LLVMContext &context = M.getContext();
    FunctionType *postinjectfunctype = FunctionType::get(
        Type::getVoidTy(context), false); 
    Constant *postracingfunc = M.getOrInsertFunction("postTracing",
                                             postinjectfunctype);


 std::set<Instruction*> exitinsts;
     getProgramExitInsts(M, exitinsts);
     assert (exitinsts.size() != 0 
             && "Program does not have explicit exit point");
 
     for (std::set<Instruction*>::iterator it = exitinsts.begin();
          it != exitinsts.end(); ++it) {
       Instruction *term = *it;
       CallInst::Create(postracingfunc, "", term);
     }
    return true;
  }

  long fetchLLFIInstructionID(Instruction *targetInst) {
    return llfi::getLLFIIndexofInst(targetInst);
  }

  virtual bool runOnFunction(Function &F) {
    //Create handles to the functions parent module and context
    LLVMContext& context = F.getContext();
    Module *M = F.getParent();

    //iterate through each basicblock of the function
    inst_iterator lastInst;
    for (inst_iterator instIterator = inst_begin(F), 
         lastInst = inst_end(F);
         instIterator != lastInst; ++instIterator) {

        //Print some Debug Info as the pass is being run
      Instruction *inst = &*instIterator;

      if (debugtrace) {
        if (!llfi::isLLFIIndexedInst(inst)) {
          errs() << "Instruction " << *inst << " was not indexed\n";
        } else {
          errs() << "Instruction " << *inst << " was indexed\n";
        }
      }
      if (llfi::isLLFIIndexedInst(inst)) {

        //Find instrumentation point for current instruction
        Instruction *insertPoint;
        if (!inst->isTerminator()) {
          insertPoint = llfi::getInsertPtrforRegsofInst(inst, inst);
        } else {
          insertPoint = inst;
        }



        //Fetch size of instruction value
        //The size must be rounded up before conversion to bytes because some data in llvm
        //can be like 1 bit if it only needs 1 bit out of an 8bit/1byte data type
        float bitSize;
        AllocaInst* ptrInst;
        int flag = 0; // 0: int 1: floating point 2: void
        ConstantInt* intConst = NULL;
        ConstantFP* fpConst = NULL;
        CastInst* castinst = NULL;

        if (inst->getType() != Type::getVoidTy(context)) {

          if ((inst->getType())->isFloatingPointTy()){
              flag = 1;
              castinst = CastInst::CreateFPCast(inst,Type::getDoubleTy(context),"castfloat",insertPoint);
              intConst = ConstantInt::get(IntegerType::get(context, 64), 0);
          }
          else if ((inst->getType())->isIntegerTy()){
              flag = 0;
              castinst = CastInst::CreateSExtOrBitCast(inst,Type::getInt64Ty(context),"castint",insertPoint);
              fpConst = ConstantFP::get(context, APFloat(0.0));
          }
          else if ((inst->getType())->isPointerTy()){
              flag = 0;
              castinst = CastInst::CreatePointerCast(inst,Type::getInt64Ty(context),"castpointer",insertPoint);
              fpConst = ConstantFP::get(context, APFloat(0.0));
          }
          else{
               fpConst = ConstantFP::get(context, APFloat(double(0.0)));
               intConst = ConstantInt::get(IntegerType::get(context, 64), 0);
               flag = 2;
          }
          DataLayout &td = getAnalysis<DataLayout>();
          bitSize = (float)td.getTypeSizeInBits(inst->getType());
        }
        else {
          bitSize = 32;
          flag = 2;
          fpConst = ConstantFP::get(context, APFloat(double(0.0)));
          intConst = ConstantInt::get(IntegerType::get(context, 64), 0);
        }
        int byteSize = (int)ceil(bitSize / 8.0);




        int opcodeIndex = inst->getOpcode();

        //Create the decleration of the printInstTracer Function
        std::vector<Type*> parameterVector(7);
        parameterVector[0] = Type::getInt32Ty(context); //ID
        parameterVector[1] = Type::getInt32Ty(context);     //OpCode ID
        parameterVector[2] = Type::getInt32Ty(context); //Size of Inst Value
        parameterVector[3] = Type::getDoubleTy(context); //Floating point value
        parameterVector[4] = Type::getInt32Ty(context); //Int of max traces
        parameterVector[5] = Type::getInt64Ty(context); //Int value
        parameterVector[6] = Type::getInt32Ty(context); //Flag
   
        
        // LLVM 3.3 Upgrade
        ArrayRef<Type*> parameterVector_array_ref(parameterVector);

        FunctionType* traceFuncType = FunctionType::get(Type::getVoidTy(context), 
                                                        parameterVector_array_ref, false);
        Constant *traceFunc = M->getOrInsertFunction("printInstTracer", traceFuncType); 

        //Insert the tracing function, passing it the proper arguments
        std::vector<Value*> traceArgs;
        //Fetch the LLFI Instruction ID:
        ConstantInt* IDConstInt = ConstantInt::get(IntegerType::get(context, 32), 
                                                   fetchLLFIInstructionID(inst));

        ConstantInt* instValSize = ConstantInt::get(
                                      IntegerType::get(context, 32), byteSize);

        //Fetch maxtrace number:
        ConstantInt* maxTraceConstInt =
            ConstantInt::get(IntegerType::get(context, 32), maxtrace);
        
        ConstantInt* opcodeConstInt =
            ConstantInt::get(IntegerType::get(context, 32), opcodeIndex);
        ConstantInt* flagConstInt =
            ConstantInt::get(IntegerType::get(context, 32), flag);
          

        //Load All Arguments
        if (flag == 0){
            traceArgs.push_back(IDConstInt);
            traceArgs.push_back(opcodeConstInt);
            traceArgs.push_back(instValSize);
            traceArgs.push_back(fpConst);
            traceArgs.push_back(maxTraceConstInt);
            traceArgs.push_back(castinst);
            traceArgs.push_back(flagConstInt);

        }
        else if (flag == 1){
            traceArgs.push_back(IDConstInt);
            traceArgs.push_back(opcodeConstInt);
            traceArgs.push_back(instValSize);
            traceArgs.push_back(castinst);
            traceArgs.push_back(maxTraceConstInt);
            traceArgs.push_back(intConst);
            traceArgs.push_back(flagConstInt);
        }
        else {
            traceArgs.push_back(IDConstInt);
            traceArgs.push_back(opcodeConstInt);
            traceArgs.push_back(instValSize);
            traceArgs.push_back(fpConst);
            traceArgs.push_back(maxTraceConstInt);
            traceArgs.push_back(intConst);
            traceArgs.push_back(flagConstInt);
        }
        // LLVM 3.3 Upgrade
        ArrayRef<Value*> traceArgs_array_ref(traceArgs);

        //Create the Function
        CallInst::Create(traceFunc, traceArgs_array_ref, "", insertPoint);
      }
    }//Function Iteration

    return true; //Tell LLVM that the Function was modified
  }//RunOnFunction
};//struct InstTrace

//Register the pass with the llvm
char InstTrace::ID = 0;
static RegisterPass<InstTrace> X("insttracepass", 
    "Add tracing function calls in program to trace instruction value at runtime", 
    false, false);

}//namespace llfi
