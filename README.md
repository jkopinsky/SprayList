SprayList
=========

The SprayList data structure

PREREQUISITES
-------------

gcc 4.6
gsl (should be removed soon)


INSTALL
-------

Compile using make


RUN
---

Run the executable at
./bin/spray

-h option to view parameters
Default experiment uses Fraser's skiplist to alternatingly insert a random value and then remove the successor of another random value.


EXPERIMENTS
-----------

Several run scripts are included to execute the experiments described in the paper. The following do not take any inputs:
--run_throughput.sh: Run the throughput experiment described in Section 4.1 (Figure 3)
--run_sssp_all.sh: Run the SSSP experiments described in Section 4.3 (Figure 5)
--run_des.sh: Run the Discrete Event Simulation experiment described in Section 4.4 (Figure 6)
