
The IP packages follow the following naming convention:

```
[target]_NPU_IP_O[AIE_Offset]_A[AIE_number]_D[DDRS_number].tgz
```

where:

* `target`       : chip for which the IP (AIE/RTL code) was generated for.
* `AIE_Offset`   : most left AIE column from which the partition allocation starts.
* `AIE_number`   : Number of AIE used. Full column of AIE are used.
* `DDRS_number`  : Number of external memories required by the IP.

Example : `vitis_ai_V2024.1_VE2802_NPU_IP_O0_A128_M3.tgz`

