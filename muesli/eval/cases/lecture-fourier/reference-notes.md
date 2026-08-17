## Key concepts

- The Fourier transform decomposes a signal into sinusoids: F(ω) = ∫ f(t) e^(−iωt) dt, interpreted as correlating the signal against every frequency. <!--m:{"spans":[[0,1]]}-->
- Time–frequency duality: narrow in time means wide in frequency; a perfect impulse contains every frequency equally. <!--m:{"spans":[[5,5]]}-->
- Convolution in time equals multiplication in frequency; filtering is multiplying the spectrum by a window. <!--m:{"spans":[[4,4]]}-->

## Examples & derivations

- Pure 5 Hz sine → two spikes at ±5 Hz, nothing else. <!--m:{"spans":[[2,2]]}-->
- Square wave → fundamental plus all odd harmonics falling off as 1/n, which is why square waves sound buzzy. <!--m:{"spans":[[3,3]]}-->

## Questions to review

- Convolution ↔ multiplication property — explicitly flagged for the midterm. <!--m:{"spans":[[4,4]]}-->
- Next lecture: the DFT and why the FFT makes it practical. <!--m:{"spans":[[6,6]]}-->
