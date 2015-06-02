
#include "raffo.peg"
#include "fft.h"
#include "raffo.h"

// max_samples: cantidad maxima de samples que se procesan por llamado a render()
// un numero mayor resulta en mejor performance, pero peor granularidad en la transición de frecuencias
#define max_samples 256
#define ATTACK ((*p(m_attack) + 2) * 100)
#define DECAY (*p(m_decay)*100 + .1)
#define SUSTAIN pow(*p(m_sustain), 2)
#define RELEASE *p(m_release)
#define FILTER_ATTACK ((*p(m_filter_attack) + 2) * 100)
#define FILTER_DECAY (*p(m_filter_decay)*100 + .1)
#define FILTER_SUSTAIN *p(m_filter_sustain)
#define FILTER_RELEASE *p(m_filter_release)

using namespace std;

extern "C" void ondaTriangular(uint32_t from, uint32_t to, uint32_t counter, float* buffer, float subperiod, float vol, float env);

extern "C" void ondaSierra(uint32_t from, uint32_t to, uint32_t counter, float* buffer, float subperiod, float vol, float env);

extern "C" void ondaPulso(uint32_t from, uint32_t to, uint32_t counter, float* buffer, float subperiod, float vol, float env);

extern "C" void ondaCuadrada(uint32_t from, uint32_t to, uint32_t counter, float* buffer, float subperiod, float vol, float env);

extern "C" void nada();

RaffoSynth::RaffoSynth(double rate):
  Parent(m_n_ports),
  sample_rate(rate),
  dt(1./rate),
  period(500),
  glide_period(500),
  counter(0),
  pitch(1),
  primer_nota(true)
  {
    midi_type = Parent::uri_to_id(LV2_EVENT_URI, "http://lv2plug.in/ns/ext/midi#MidiEvent"); 
    prev_vals[0] = prev_vals[1] = prev_vals[2] = prev_vals[3] = prev_vals[4] = prev_vals[5] = 0;
  }
     
     
void RaffoSynth::render(uint32_t from, uint32_t to) {
  //if (keys.empty()) return;
  
   t_osc.start();
  // buffer en 0
  for (uint32_t i = from; i < to; ++i) p(m_output)[i] = 0;
  
  double glide_factor;
  if (*p(m_glide) < .1) {
    glide_period = period;
    glide_factor = 1;
  } else {
    glide = pow(2., (to-from) / (sample_rate * (*p(m_glide)/5.))) ;
    glide_factor = min_fact(((glide_period < period)? glide : 1. / glide), 
                         period/glide_period);
    glide_period *= glide_factor;
  }
  
  // osciladores
  //int envelope_subcount;
  
  float* buffer = p(m_output);
  for (int osc = 0; osc < 4; osc++) {
    if (*p(m_oscButton0 + osc) == 1){ //Si el botón del oscilador está en 1, se ejecuta render
      //envelope_subcount = envelope_count;
      float vol = pow(*p(m_volume) * *p(m_vol0 + osc) / 100., .5)/4; // el volumen es el cuadrado de la amplitud
      float subperiod = glide_period / (pow(2,*p(m_range0 + osc))  * pitch * pow(2, *p(m_tuning0 + osc) / 12.) ); // periodo efectivo del oscilador
    
      // valores precalculados para el envelope
      // la función de envelope es:
        // f(t) = s - (1-s)/(2*d) * (t-a-d-|t-a-d|) + (1/(2*a) + (1-s)/(2*d)) * (t-a-|t-a|)
        /*
              /\
             /  \
            /    \_______________  -> s = sustain level
           /  
          /
          |-a-|-d-|--------------|
        */

      float env = envelope(envelope_count, ATTACK, DECAY, SUSTAIN);
      counter = last_val[osc] * subperiod;

      switch ((int)*p(m_wave0 + osc)) {
        case (0): { //triangular
          // ASM
          // ondaTriangular(from, to, counter, buffer, subperiod, vol, env);

          //  C
          for (uint32_t i = from; i < to; ++i, counter++) {
            buffer[i] += vol * (4. * (fabs(fmod(((counter) + subperiod/4.), subperiod) /
                              subperiod - .5)-.25)) * env;
          }
          // zapato: la onda triangular esta hecha para que empiece continua, pero cuando se corta popea
          break;
        }
        case (1): { //sierra
          //ASM
          ondaSierra(from, to, counter, buffer, subperiod, vol, env);
          counter+= (to - from);

          //C
          // for (uint32_t i = from; i < to; i+=4, counter+=4) {
          //   buffer[i] += vol * (2. * fmod(counter, subperiod) / subperiod - 1) * env;
          // }
          break;
        }
        case (2): { //cuadrada
          //ASM
          ondaCuadrada(from, to, counter, buffer, subperiod, vol, env);
          counter+= (to-from);
          //C
          // for (uint32_t i = from; i < to; ++i, counter++) {
          //   buffer[i] += vol * (2. * ((fmod(counter, subperiod) / subperiod - .5) < 0)-1) * env;
          // }
          break;
        }
        case (3): { //pulso
          //ASM
          ondaPulso(from, to, counter, buffer, subperiod, vol, env);
          counter+= (to-from);

          //C
          // for (uint32_t i = from; i < to; ++i, counter++) {
          //   buffer[i] += vol * (2. * ((fmod(counter, subperiod) / subperiod - .2) < 0)-1) * env;
          // }
          break;
        }
      }
    last_val[osc] = fmod(counter, subperiod) / subperiod; //para ajustar el enganche de la onda entre corridas de la funcion
    } //Fin del if
  } //Fin del for
  t_osc.stop();
  //counter = counter % (int)glide_period;
  
}
  
void RaffoSynth::handle_midi(uint32_t size, unsigned char* data) {
  if (size == 3) {
    switch (data[0]) {
      case (0x90): { // note on
        if (keys.empty()) {
          //envelope_count = 0;
          if (primer_nota) {
            glide_period = sample_rate * 4 / key2hz(data[1]); // la primera nota no tiene glide
            primer_nota = false;
          }
          counter = 0;
        }
        keys.push_front(data[1]);
        period = sample_rate * 4 / key2hz(data[1]);
        break;
      }
      case (0x80): { // note off
        keys.remove(data[1]);
        if (keys.empty()) {
          // poner los contadores de adsr en el lugar correcto
          envelope_count = envelope(envelope_count, ATTACK, DECAY, SUSTAIN) * ATTACK;
          envelope_count *= (envelope_count>0);
          filter_count = envelope(filter_count, FILTER_ATTACK, FILTER_DECAY, FILTER_SUSTAIN) * FILTER_ATTACK;
          filter_count *= (filter_count>0);
        } else {
          period = sample_rate * 4 / key2hz(keys.front());
        }
        //if (!keys.empty()) period = sample_rate * 2 / key2hz(keys.front());
        break;
      }
      case (0xE0): { // pitch bend
        /* Calculamos el factor de pitch (numero por el que multiplicar 
           la frecuencia fundamental). data[2] es el byte mas significativo, 
           data[1] el menos. El primer bit de ambos es 0, por eso << 7. 
           pitch_width es el numero maximo de semitonos de amplitud del pitch.
        * Mas informacion: http://sites.uci.edu/camp2014/2014/04/30/managing-midi-pitchbend-messages/
        */
        pitch = pow(2.,(((data[2] << 7) ^ data[1]) / 8191. - 1) / 6.); 
      }  
    }
  }
} /*handle_midi*/

void RaffoSynth::run(uint32_t sample_count) {

  run_count++;
  t_run.start();

  LV2_Event_Iterator iter;
  lv2_event_begin(&iter, reinterpret_cast<LV2_Event_Buffer*&>(Parent::m_ports[m_midi]));

  uint8_t* event_data;
  uint32_t samples_done = 0;
  while (samples_done < sample_count) {
    uint32_t to = sample_count;
    LV2_Event* ev = 0;
    if (lv2_event_is_valid(&iter)) {
      ev = lv2_event_get(&iter, &event_data);
      to = ev->frames;
      lv2_event_increment(&iter);
    }
    if (to > samples_done) {
      if (keys.empty()) { // actualizamos los envelopes
        envelope_count *= (1 - pow(RELEASE - 1, 2)) /* ((to-samples_done) / sample_count) **/ ;
        //if (envelope_count < 0) envelope_count=0; //envelope_count *= (envelope_count > 0);
        filter_count *= (1 - pow(FILTER_RELEASE - 1, 2));
        //if (filter_count < 0) filter_count=0; //filter_count *= (filter_count > 0);
      } else {
          envelope_count += to - samples_done;
          filter_count += to - samples_done;
      }
      while (samples_done + max_samples < to) { // subdividimos el buffer en porciones de tamaño max_sample
        render(samples_done, samples_done + max_samples);
        samples_done += max_samples;
      }
      render(samples_done, to);
      samples_done = to;
    }

    if (ev) {
      if (ev->type == midi_type)
        static_cast<RaffoSynth*>(this)->handle_midi(ev->size, event_data);
    }
  }
  
  // EQ 
  t_eq.start();
  ir(sample_count);
  t_eq.stop();

  t_run.stop();
  //cout << run_count << " " << t_run.time << " " << t_osc.time << " " << t_eq.time << endl;
} /*run*/
  
void RaffoSynth::ir(int sample_count) { 
  //http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
  
  // variables precalculadas
 
  float env = envelope(filter_count, FILTER_ATTACK, FILTER_DECAY, FILTER_SUSTAIN);
  
  float w0 = 6.28318530717959 * (*p(m_filter_cutoff) * env + 100) / sample_rate;
  float alpha = sin(w0)/4.; // 2 * Q,  Q va a ser constante, por ahora = 2
  float cosw0 = cos(w0);

  float lpf_a0 = 1 + alpha;
  float lpf_a1 = - 2 * cosw0 / lpf_a0;
  float lpf_a2 = (1 - alpha) / lpf_a0;
  float lpf_b1 = (1 - cosw0) / lpf_a0;
  float lpf_b0 = lpf_b1 / 2;

  float gain_factor = pow(10., *p(m_filter_resonance)/20.);
  float peak_w0 = 6.28318530717959 * (*p(m_filter_cutoff) * env + 100) * 0.9 / sample_rate;
  float peak_alpha = sin(peak_w0)/4.; // 2 * Q,  Q va a ser constante, por ahora = 2
  float cos_peak_w0 = cos(peak_w0);
  float peak_a0 = 1 + peak_alpha / gain_factor;
  float peak_a1 = -2 * cos_peak_w0 / peak_a0;
  float peak_a2 = (1 - peak_alpha / gain_factor) / peak_a0;
  float peak_b0 = (1 + peak_alpha * gain_factor) / peak_a0;
  float peak_b1 = - 2 * cos_peak_w0 / peak_a0;
  float peak_b2 = (1 - peak_alpha * gain_factor) / peak_a0;


  //cout << "in: " << p(m_output)[0];cout << " out: " << p(m_output)[0] << " filter_count: " << filter_count << endl;
  for (int i = 0; i < sample_count; i++) {
    //low-pass filter     

    float temp = p(m_output)[i];
    p(m_output)[i] *= lpf_b0;
    p(m_output)[i] += lpf_b1 * prev_vals[1] + lpf_b0 * prev_vals[0] 
                    - lpf_a1 * prev_vals[3] - lpf_a2 * prev_vals[2];
    prev_vals[0] = prev_vals[1];
    prev_vals[1] = temp;
    
    // peaking EQ (resonance)
    float temp2 = p(m_output)[i];

    p(m_output)[i] *= peak_b0;
    p(m_output)[i] += peak_b1 * prev_vals[3] + peak_b2 * prev_vals[2] 
                    - peak_a1 * prev_vals[5] - peak_a2 * prev_vals[4];
    prev_vals[2] = prev_vals[3];
    prev_vals[3] = temp;
    prev_vals[4] = prev_vals[5];
    prev_vals[5] = p(m_output)[i];

    // filter ads
    // p(m_output)[i] *= env;
    // p(m_output)[i] += (1-env) * temp;
    // count++;
     
  }
    

}
static int _ = RaffoSynth::register_class(m_uri);

