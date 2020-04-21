#pragma once

#include <cmath>
#include "Oscillator.hpp"
#include "RingModulator.hpp"
#include "Interpolate.hpp"

#define NUM_OSCILLATORS 36

struct BankOutput {
  float outputMono;
  float outputLeft;
  float outputRight;
};

struct OscillatorBank {
  OscillatorBank() {
    magnitudeSmoothness = 50; // Manually Adjust
    amplitudeSmoothness = 50; // Manuallly adjust
  }

  void setFrequency(float *frequencies, float *volumes, uint8_t count) {
    numOscillators = count;

    // update the current bank values
    for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
      if (i < count) {
        currentFrequencies[i] = frequencies[i];
        currentVolumes[i] = volumes[i];
      } else {
        currentFrequencies[i] = 0;
        currentVolumes[i] = 0;
      }
    }
  }

  void setFM(uint8_t *fmMatrix, float *fmAmount, float *fmIn, uint8_t count) {
    for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
      if (count == 0) {
        this->frequencyModulation[i] = 0;
      } else {        
        uint8_t j = fmMatrix[i];
        while (j >= count) {
          j -= count;
        }
        this->frequencyModulation[i] = fmIn[j] * fmAmount[i];
      }
    }
  }

  void setRM(bool rmActive, uint8_t *rmMatrix, float *rmMix, float *amInput, uint8_t count) {
    this->ringModuationInternal = rmActive;
    for (uint8_t i = 0; i < NUM_OSCILLATORS;i++) {
      this->rmMatrix[i]  = rmMatrix[i];
      this->rmMix[i] = rmMix[i];
    }

    for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
      if (count == 0) {
        this->amplitudeModulation[i] = 1;
      } else {        
        uint8_t j = rmMatrix[i];
        while (j >= count) {
          j -= count;
        }
        float amAmount = amInput[j] / 5.0; //Scale input
        if(amAmount != amplitudeModulation[i]) {
          oldAmplitudeModulation[i] = amplitudeModulation[i];
          currentAMSmoothingPosition = 0;
        }
        this->amplitudeModulation[i] = amAmount;
      }
    }

  }


  void setVoiceShift(int16_t shift) {
    this->voiceShift = shift;
  }


  void switchBanks() {
    currentMagnitudeSmoothingPosition = 0;
    // update the current bank values
    for (uint8_t i = 0; i < NUM_OSCILLATORS; i++) {
      oldFrequencies[i] = currentFrequencies[i];
      oldVolumes[i] = currentVolumes[i];
    }
  }


  BankOutput process(uint8_t waveShape, float sampleTime,float *panning) {
    uint8_t currentVoice = 0;
    float_4 oscFreq = 0;
    float volumes[NUM_OSCILLATORS];

    currentMagnitudeSmoothingPosition++;
    if (currentMagnitudeSmoothingPosition > magnitudeSmoothness) {
      currentMagnitudeSmoothingPosition = magnitudeSmoothness;
    }

    float pct = magnitudeSmoothness ? (float(currentMagnitudeSmoothingPosition) / float(magnitudeSmoothness)) : 1;
    for (uint8_t i = 0, c = 0; i < NUM_OSCILLATORS; ) {
      for (c = 0; c < 4; c++) {
        if (i + c < numOscillators) {
          oscFreq[c] = currentFrequencies[i + c] + frequencyModulation[i + c];
          volumes[i + c] =  interpolate(oldVolumes[i + c],currentVolumes[i + c],pct,0.0f,1.0f);
        }
      }

      // set up the current bank and step it
      bank[currentVoice].setFrequency(oscFreq);
      bank[currentVoice].step(sampleTime);

      currentVoice++;
      i += c;
    }

    currentVoice = 0;
    float outputMono = 0;
    float outputLeft = 0;
    float outputRight = 0;

    for (uint8_t i = 0, c = 0; i < NUM_OSCILLATORS; ) {
      float_4 oscOutput;
      switch (waveShape) {
        case SIN_WAVESHAPE :
          oscOutput = bank[currentVoice].sin();
          break;
        case TRIANGLE_WAVESHAPE :
          oscOutput = bank[currentVoice].tri();
          break;
        case SAWTOOTH_WAVESHAPE :
          oscOutput = bank[currentVoice].saw();
          break;
        case SQUARE_WAVESHAPE :
          oscOutput = bank[currentVoice].sqr();
          break;
        case RECTANGLE_WAVESHAPE :
          oscOutput = bank[currentVoice].rect();
          break;
      }

      for (c = 0; c < 4 && i + c < NUM_OSCILLATORS; c++) {
        if (i + c < numOscillators) {
          if (numOscillators) {
            int8_t magnitudeVoice = (i + c + voiceShift) % numOscillators;
            if (magnitudeVoice < 0) {
              magnitudeVoice += numOscillators;
            }
            currentVoiceOutput[i+c] = (oscOutput[c] * volumes[magnitudeVoice]);
          } else {
            currentVoiceOutput[i+c] = (oscOutput[c] * volumes[(i + c)]);
          }
        }
      }
      currentVoice++;
      i += c;
    }


    currentAMSmoothingPosition++;
    if (currentAMSmoothingPosition > amplitudeSmoothness) {
      currentAMSmoothingPosition = amplitudeSmoothness;
    }

    float smoothPct = amplitudeSmoothness ? (float(currentAMSmoothingPosition) / float(amplitudeSmoothness)) : 1;

    for (uint8_t i = 0; i < NUM_OSCILLATORS && i < numOscillators; i++) {
      float currentAMAmount = interpolate(oldAmplitudeModulation[i],amplitudeModulation[i],smoothPct,0.0f,1.0f);
      float amValue = currentVoiceOutput[i] * (currentAMAmount > 0 ? currentAMAmount : 0); 
      float rmValue = ringModulator.processModel(currentVoiceOutput[i],currentVoiceOutput[rmMatrix[i]]);

      float adjustedValue = interpolate(currentVoiceOutput[i],ringModuationInternal ? rmValue : amValue ,rmMix[i],0.0f,1.0f);
      outputMono += adjustedValue;
      outputLeft += adjustedValue * std::max(1.0 - panning[i],0.0) ;
      outputRight += adjustedValue * std::max(panning[i]+1.0,1.0);
    }

    BankOutput bankOutput;
    bankOutput.outputMono = outputMono;
    bankOutput.outputLeft = outputLeft;
    bankOutput.outputRight = outputRight;

    return bankOutput;
  }


  uint8_t numOscillators;
  Oscillator<float_4> bank[NUM_OSCILLATORS / 4];
  RingModulator ringModulator;
  bool ringModuationInternal;
  float currentFrequencies[NUM_OSCILLATORS]{0};
  float currentVolumes[NUM_OSCILLATORS]{0};
  float currentVoiceOutput[NUM_OSCILLATORS];
  uint16_t magnitudeSmoothness;
  uint16_t currentMagnitudeSmoothingPosition;
  uint16_t amplitudeSmoothness;
  uint16_t currentAMSmoothingPosition = 0;
  float oldFrequencies[NUM_OSCILLATORS]{0};
  float oldVolumes[NUM_OSCILLATORS]{0};
  float oldAmplitudeModulation[NUM_OSCILLATORS]{0};
  int8_t voiceShift = 0;
  float frequencyModulation[NUM_OSCILLATORS + 4]{0};
  float amplitudeModulation[NUM_OSCILLATORS + 4]{0};
  uint8_t rmMatrix[NUM_OSCILLATORS] {0};
  float rmMix[NUM_OSCILLATORS]{0};
};
