//DOM-IGNORE-BEGIN
/*
Copyright (C) 2025, Microchip Technology Inc., and its subsidiaries. All rights reserved.

The software and documentation is provided by microchip and its contributors
"as is" and any express, implied or statutory warranties, including, but not
limited to, the implied warranties of merchantability, fitness for a particular
purpose and non-infringement of third party intellectual property rights are
disclaimed to the fullest extent permitted by law. In no event shall microchip
or its contributors be liable for any direct, indirect, incidental, special,
exemplary, or consequential damages (including, but not limited to, procurement
of substitute goods or services; loss of use, data, or profits; or business
interruption) however caused and on any theory of liability, whether in contract,
strict liability, or tort (including negligence or otherwise) arising in any way
out of the use of the software and documentation, even if advised of the
possibility of such damage.

Except as expressly permitted hereunder and subject to the applicable license terms
for any third-party software incorporated in the software and any applicable open
source software license terms, no license or other rights, whether express or
implied, are granted under any patent or other intellectual property rights of
Microchip or any third party.
*/
//DOM-IGNORE-END

#include "filters.h"
 
#define SGN(x)      ((x>0) - (x<0))
double lowPassExponential(double input, double average, double factor)
{
  return input*factor + (1-factor)*average;  /* factor must be in [0,1] */
}

const double fine_filter_coeff[FIR_FILER_SIZE_FINE] = {1.0, 1.0, 1.0};
double firLowPassFilter(int32_t input, lpfState* state)
{
  double ret = 0.0;
  double div = 0.0;
  double diffs = 0.0;
  uint32_t pos = state->head;
  uint32_t last_pos;

  state->buffer[state->head] = input;

  if(state->filled < state->filterSize) {
    state->filled++;
  }
  
  for(uint32_t i=0; i<state->filled; i++)
  {
    ret += (double)((double)state->buffer[pos]*fine_filter_coeff[i]);
    div += fine_filter_coeff[i];
    
    if(i>0)
    {
        double temp;
        temp = (double)state->buffer[last_pos] - (double)state->buffer[pos];
        if( (temp<((-1)*(CLOCK_CYCLE_NS-CLOCK_OFFsET_NS)) ) || (temp>(CLOCK_CYCLE_NS-CLOCK_OFFsET_NS)) )
        {
            diffs += SGN(temp)*(CLOCK_CYCLE_NS/(FIR_FILER_SIZE_FINE-1));
        }
    }
    last_pos = pos;
    if(pos == 0) 
    {
      pos = state->filterSize - 1u;
    } 
    else 
    {
      pos--;
    }
  }
  
  state->head = (state->head + 1u) % state->filterSize;
  ret = ret / div;
  ret += diffs;
  return ret;
}

double firLowPassFilterF(double input, lpfStateF* state)
{
  double ret = 0.0;
  uint32_t pos = state->head;

  state->buffer[state->head] = input;

  if(state->filled < state->filterSize) {
    state->filled++;
  }
  
  for(uint32_t i=0; i<state->filled; i++)
  {
    ret += state->buffer[pos];
    
    if(pos == 0) {
      pos = state->filterSize - 1u;
    } else {
      pos--;
    }
  }

  state->head = (state->head + 1u) % state->filterSize;
  
  return ret / state->filled;
}
