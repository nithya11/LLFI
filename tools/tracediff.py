#!/usr/bin/python

#traceDiff.py
#  Author: Sam Coulter
#  This python script is part of the greater LLFI system.
#  This script will examine two tracing output files generated by running a program after
#  the LLFI traceInst pass has been performed.
#   Exec: traceDiff.py goldenTrace faultyTrace
#  Input: GoldenTrace/faultyTrace - Trace output files after running a traced program
#  Output: Trace Summary into Standard output, redirect with PIPE to save to file


import sys
import os
import glob
import difflib
from tracetools import *

def traceDiff(argv, output = 0):
  #save stdout so we can redirect it without mangling other python scripts
  oldSTDOut = sys.stdout
  
  if output != 0:
    sys.stdout = open(output, "w")
  if (len(argv) != 3):
    print "ERROR: running option: traceDiff <golden output> <faulty output>"
    exit(1)

  goldFile = open(argv[1], 'r')
  goldTrace = goldFile.read()
  goldFile.close()

  faultyFile = open(argv[2], 'r')
  faultyTrace = faultyFile.read()
  faultyFile.close()

  goldTraceLines = goldTrace.split("\n")
  goldTraceLines = goldTrace.split("\n")
  faultyTraceLines =  faultyTrace.split("\n")

  #Examine Header of Trace File
  header = faultyTraceLines[0].split(' ')
  for i in range(0, len(header) - 1):
    keyword = header[i]
    if keyword == "#TraceStartInstNumber:":
    #Remove traces from golden trace that happened before fault injection point
      faultyTraceStartPoint = int(header[i+1])
      faultyTraceLines.pop(0)
      for i in range (0,faultyTraceStartPoint-1):
        goldTraceLines.pop(0)

  #record and report the fault injected line
  goldInjectedLine = diffLine(goldTraceLines[0])
  faultInjectedLine = diffLine(faultyTraceLines[0])
  diffID = goldInjectedLine.ID
  print "#FaultReport"
  print "1 @", faultyTraceStartPoint
  print goldInjectedLine.raw, "/", faultInjectedLine.Value

  #remove the fault injected lines
  goldTraceLines.pop(0)
  faultyTraceLines.pop(0)

  for line in goldTraceLines:
    if line == "":
      goldTraceLines.remove(line)

  for line in faultyTraceLines:
    if line == "":
      faultyTraceLines.remove(line)

  lenGT = len(goldTraceLines) - 1
  lenFT = len(faultyTraceLines) - 1
  
  i = 0

  if (lenGT < 0 and lenFT < 0):
    return 0

  while (faultyTraceLines[lenFT-i] == goldTraceLines[lenGT-i]):
    postDiffID = diffLine(goldTraceLines[lenGT-i]).ID
    faultyTraceLines.pop(lenFT-i)
    goldTraceLines.pop(lenGT-i)    
    i = i + 1
    if lenFT-i < 0 or lenGT-i < 0:
      break

  diff = list(difflib.unified_diff(goldTraceLines, faultyTraceLines, n=0, lineterm=''))

  if (diff):
    diff.pop(0) #Remove File Context lines from diff
    diff.pop(0)

    #print '\n'.join(diff) #For debugging

    report = diffReport(diff, faultyTraceStartPoint)
    report.printSummary()

  #restore stdout
  sys.stdout = oldSTDOut

if (__name__ == "__main__"):
  traceDiff(sys.argv)
