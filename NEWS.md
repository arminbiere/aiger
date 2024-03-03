## Version 1.9.18

  - Dedicated ITE detection and CNF encoding for 'aigtocnf'.

## Version 1.9.17

  - Dedicated XOR detection and CNF encoding for 'aigtocnf'.

  - Added '-r' option for 'aigselect'.

  - First output needs to be negated in 'aigor'
    (discovered by Gianpiero Cabodi in 2021).

  - Reset types during reencoding (discovered by Sam Bayless in 2016).

## Version 1.9.16
  
  - Only warn if there are no constraints in 'aigunconstraint'.
  - Added better parse error reporting for 'c ' versus 'c<new-line>'.
  - Added better parse error context for failed literal reading.
  - Added '-u' option for 'aigunroll'.
  - Added 'aiger[de]interleave.sh' and 'aigerswap[2].sh'
  - Fixed 'aigtocnf' to properly respect inputs even if output is constant.
  - Allow 'x' for initialized latches in 'aigsim' (thanks to Norbert Manthey)
  - Added '--remove-outputs' to 'aigtoaig'.
  - Fixed 'aigflip' symbol issue  (thanks to Heinz Riener).
  - Added '-f' option to 'aigsim' to filter lines.
  - Added 'aigdep' to list dependent variables (not working yet).

## Version 1.9.8
  
  - Added 'aigunconstraint' tool to remove environment constraints.
  - Run 'aigbmc' up to maximum bound or until all properties satisfied.
  - Optional 'Lingeling' back-end for 'aigbmc' (with cloning).
  - Using new reentrant interface for PicoSAT in 'aigbmc'.
  - Fixed 'aigtosmv' to produce correct 'init' statements.
  - Added 'aigreset' to normalize reset.
  - Fixed 'mc.sh' to use 'aigunroll'.
  - One and not initialized BLIF latches patch for 'bliftoaig' by Fabio Somenzi.

## Version 1.9.4

  - Improved 'aigdd' to maintain failure inducing '<dst>' file.
  - Added 'aigflip' (now actually also in the release package).
  - Some fixes to 'aigtosmv'.
  - Adapted 'smvtoaig', 'aigtosmv', 'aigsim', 'aigsplit' to new BCJF format.
  - Added 'aigmove' and 'aigmiter'.
  - Started new 'aigbmc'.
  - Renamed 'aigbmc' to 'aigunroll'.
  - Added new BCJF sections.
  - Fixed reencoding.
  - Added 'aigsplit',  'aigfuzz', 'aigand', 'aigor', and 'aigjoin'.
  - Removed memory leak for empty line comments.
  - Added command line options for executable in 'aigdd'.
  - More stable 'aigdd' to work with shell scripts as well.
  - Added binary read/write mode for compatability with Windows.
  - Optional '-p <prefix>' option to 'aigtosmv'.

## Version 20071012
  
  - Optionally include or strip symbols in visiualizers 'aigvis', 'aigtodot'.
  - Delta debugger 'aigdd' simplifies AND gates.
  - Fixed 'smvtoaig' for models with 'next' but no 'init' assignment.
  - Added 'aigvis' script (AIG->DOT->EPS->PDF, and then calls acrobat).
  - Allowed combinatorial assignments in ASSIGN section for 'smvtoaig'.
  - Fixed invalid error message if RHS contains '0' in the binary format.
  - Error in 'bliftoaig' if BLIF latches are not inizialized by '0'.
  - Fixed 'bliftoaig' (usage of 'aiger_not' instead of 'not_aig').

## Version 20070427
  
  - Simulator 'aigsim' can now check correctness of a witness.
  - Fixed minor bug in reading correct AIGER file reported as incorrect.
  - Extended 'smvtoaig' to handle 'next' operators in 'DEFINE' section.

## Version 20070308

  - Added 'aigtodot' visualizer.
  - Fixed examples to be consistent with newer symbol table entry format.
  - Produce output symbols in 'aigtoblif' even if AIGER file is stripped.

## Version 20061129

  - New poor man model checker 'mc.sh' based on AIGER tools.
  - Added 'examples/JaigerToCNF.java' (with help from Daniel Le Berre).
  - Switched to MIT style license (except for 'bliftoaig').
  - New 'wrapstim' to generate stimulus from time frame expanded solution.
  - Added stimulus and witness/counter example definitions to FORMAT report.

## Version 20061115

  - Made ASCII format default if output is connected to terminal.
  - New delta debugger for AIG tools.
  - New bounded model checker for AIGs.
  - New simple AIG library 'SimpAIG'.
  - Fixed various non-initialized data defects.

## Version 20061016
 
  - Added 'bliftoaig' and 'aigtoblif' by Mark Herbstritt.
  - Adapted LICENSE.

## Version 20061011

  - Fixed 'smvtoaig' for models with 'init' but no 'next' assignments.
  - Adapted 'andtoaig' to new API.

## Version 20061005
 
  - Beta version and second public release.

  - Renamed 'big' to 'aig' and 'aig' to 'aag' to reflect the fact that
  the old binary 'big' format is really the default format.  There is no
  '.big' file extension any more.

  - Dropped second field in symbol table entries, since it was redundant.

## Version 20060915

  - Alpha version and first public release.
