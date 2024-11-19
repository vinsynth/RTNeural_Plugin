#include "m_pd.h"
//#include <memory>
#include "../RTN_Processor.cpp"
//#include <vector>

static t_class *rtneural_tilde_class;

typedef struct _rtneural_tilde {
  t_object obj;
  t_float f;
  t_float freq;
  t_float sample_rate;
  t_int blocksize;
  t_float control_rate;
  t_float nn_sample_rate;
  t_int bypass;
  t_int n_in_chans;
  t_int n_out_chans;

	t_outlet *signal_out;

  t_float ratio;
  t_float model_loaded;

  std::vector<const t_float*> in_vec;
  std::vector<t_float> inVecSmall;
  std::vector<t_float> outVecSmall;
  std::vector< std::vector<t_float> > outVecs;

  RTN_Processor processor;

  float* interleaved_array;
  float* outbuf;

  float *in_rs;
  float *out_temp;\
} rtneural_tilde_t;  

void rtneural_tilde_bang(rtneural_tilde_t *x) {
  (void)x; // silence unused variable warning
  post("stop that!");
}  

void rtneural_tilde_load_model(rtneural_tilde_t *x, t_symbol s){
  (void)x;

  post("loading model: ");
  post(s.s_name);

  t_int test = x->processor.load_model(s.s_name, 1);
  if(test==1){
    x->model_loaded = 1;
  }

  post("model input size: %i", x->processor.m_model_input_size);
  post("model output size: %i", x->processor.m_model_output_size);

  if(x->processor.m_model_input_size!=x->n_in_chans){
    post("error: model input size does not match the number of input channels");
    post("disabling model");
    x->model_loaded = 0;
  }
  if(x->processor.m_model_output_size!=x->n_out_chans){
    post("error: model output size does not match the number of output channels");
    post("disabling model");
    x->model_loaded = 0;
  }
}  

void rtneural_tilde_bypass(rtneural_tilde_t *x, t_float f){
  x->bypass = t_int(f);

  post(f ? "Bypass ON" : "Bypass OFF");
}  


void* rtneural_tilde_new(t_floatarg n_in_chans, t_floatarg n_out_chans, t_floatarg nn_sample_rate) {  

  post("%f %f %f", n_in_chans, n_out_chans, nn_sample_rate);

  if(n_in_chans<1.f){
    n_in_chans = 1.f;
  }
  if(n_out_chans<1.f){
    n_out_chans = 1.f;
  }
  if(nn_sample_rate<=0.f){
    nn_sample_rate = sys_getsr();
  }

  post("%f %f %f", n_in_chans, n_out_chans, nn_sample_rate);

  rtneural_tilde_t *x = (rtneural_tilde_t *)pd_new(rtneural_tilde_class);

  x->n_in_chans = t_int(n_in_chans);
  x->n_out_chans = t_int(n_out_chans);
  x->nn_sample_rate = nn_sample_rate;

  x->signal_out = outlet_new(&x->obj, &s_signal);

  x->sample_rate = sys_getsr();
  x->blocksize = sys_getblksize();
  x->control_rate = x->sample_rate/t_float(x->blocksize);

  x->bypass = 0;
  x->model_loaded = 0.f;

  if (nn_sample_rate>0.f) {
    x->nn_sample_rate = nn_sample_rate;
  } else {
    x->nn_sample_rate = x->sample_rate;
  }
  x->ratio = x->nn_sample_rate/x->sample_rate;
  post("ratio: %f", x->ratio);

  x->in_vec.resize(x->n_in_chans);

  t_int rs_size = t_int(ceil(x->nn_sample_rate/x->control_rate));

  //this is needed to handle resampling of audio when the sample rate is not the same as that at which the model was trained
  t_int in_size = x->blocksize*x->n_in_chans;
  t_int in_rs_size = rs_size*x->n_in_chans;
  t_int out_temp_size = rs_size*x->n_out_chans; 
  t_int out_buf_size = x->blocksize*x->n_out_chans; 

  x->processor.initialize(x->n_in_chans, x->n_out_chans, x->ratio);

  x->interleaved_array = (float*)calloc(in_size, sizeof(float));
  x->in_rs = (float*)calloc(in_rs_size, sizeof(float));
  x->out_temp = (float*)calloc(out_temp_size, sizeof(float));
  x->outbuf = (float*)calloc(out_buf_size, sizeof(float));

  return (void *)x;
}

void rtneural_tilde_free (rtneural_tilde_t* obj) {
  free(obj->interleaved_array);
  free(obj->in_rs);
  free(obj->out_temp);
  free(obj->outbuf);
	outlet_free(obj->signal_out);
}

t_int* rtneural_tilde_perform (t_int* args) {
  rtneural_tilde_t* obj = (rtneural_tilde_t*)args[1];
  t_sample    *in =      (t_sample *)(args[2]);
  t_sample* out = (t_sample *)args[3];
  t_int n_samps = (t_int)args[4];


  if ((obj->processor.m_model_loaded==0)||((t_int)obj->bypass==1)) {
    for (t_int i = 0; i < obj->blocksize; ++i) {
      out[i] = in[i];
    }
  } else {
    obj->in_vec[0] = in;
    for (t_int j = 1; j < obj->n_in_chans; j++) {
      obj->in_vec[j] = in + j * static_cast<t_int>(obj->blocksize);
    }

    t_int n_samps_out = obj->processor.process(obj->in_vec, obj->in_rs, obj->interleaved_array, obj->out_temp, obj->outbuf, obj->blocksize);

    //deinterleave the output and put it in the output buffers
    for(t_int i = 0; i < n_samps_out; i++) {
      for (t_int j = 0; j < obj->n_out_chans; j++) {
        out[j*obj->blocksize+ i] = obj->outbuf[i*obj->n_out_chans+j];
      }
    }
  }

  return (t_int *) (args + 5);
}

void rtneural_tilde_dsp (rtneural_tilde_t* obj, t_signal** sp) {
  signal_setmultiout(&sp[1], obj->n_out_chans);
  dsp_add(rtneural_tilde_perform, 4, obj, sp[0]->s_vec, sp[1]->s_vec, (t_int)(sp[0]->s_length * sp[0]->s_nchans));

}

#if defined(_LANGUAGE_C_PLUS_PLUS) || defined(__cplusplus)
extern "C" {
  void rtneural_tilde_setup(void);
}
#endif

void rtneural_tilde_setup(void) {
  rtneural_tilde_class = class_new(gensym("rtneural~"),
    (t_newmethod)rtneural_tilde_new, 
    (t_method)rtneural_tilde_free,
    sizeof(rtneural_tilde_t), 
    CLASS_MULTICHANNEL, A_DEFFLOAT, A_DEFFLOAT, A_DEFFLOAT, 0);
  
  class_addbang(rtneural_tilde_class, rtneural_tilde_bang);
  // class_addfloat(rtneural_tilde_class, rtneural_float);
  class_addmethod(rtneural_tilde_class, (t_method)rtneural_tilde_load_model, gensym("load_model"), A_SYMBOL, 0);

  class_addmethod(rtneural_tilde_class, (t_method)rtneural_tilde_bypass, gensym("bypass"), A_FLOAT, 0);

  class_addmethod(rtneural_tilde_class, (t_method)rtneural_tilde_dsp, gensym("dsp"), A_CANT, 0);
  CLASS_MAINSIGNALIN(rtneural_tilde_class, rtneural_tilde_t, f);
}



