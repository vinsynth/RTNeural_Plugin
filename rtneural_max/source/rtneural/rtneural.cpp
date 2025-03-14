
#include "ext.h"
#include "ext_obex.h"
#include "ext_strings.h"
#include "ext_common.h"
#include "ext_systhread.h"

#include "../../RTN_Processor.cpp"


#include <vector>
using namespace std;

// a wrapper for cpost() only called for debug builds on Windows
// to see these console posts, run the DbgView program (part of the SysInternals package distributed by Microsoft)
#if defined( NDEBUG ) || defined( MAC_VERSION )
#define DPOST
#else
#define DPOST cpost
#endif

// max object instance data
typedef struct _rtneural {
  t_object m_obj;
  void	*outlet;
  t_systhread_mutex	mutex;

  float f;

  // rtneural data
  t_int epochs;
  std::vector<std::vector<float>> in_vals;
  std::vector<std::vector<float>> out_vals;
  std::vector<t_int> layers_ints;
  std::vector<std::string> layers_strings;

  std::string python_path;
  
  float learn_rate;

  t_int bypass;
  t_int n_in_chans;
  t_int n_out_chans;

  t_atom *out_list;

  float ratio;
  float model_loaded;

	float* input_to_nn;
	float* output_from_nn;

	RTN_Processor processor;

} t_rtneural; 


// prototypes
void	*rtneural_new(t_symbol *s, long argc, t_atom *argv);
void	rtneural_free(t_rtneural *x);
void	rtneural_bang(t_rtneural *x);
void	rtneural_list(t_rtneural *x, t_symbol *s, long argc, t_atom *argv);
void 	rtneural_write_json(t_rtneural *x, t_symbol s, long argc, t_atom *argv);
void 	rtneural_read_json(t_rtneural *x, t_symbol s, long argc, t_atom *argv);
void 	rtneural_load_model(t_rtneural *x, t_symbol s, long argc, t_atom *argv);
void 	rtneural_bypass(t_rtneural *x, long f);

void 	rtneural_set_epochs(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_set_layers_data(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_set_learn_rate(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_clear_points(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_post_points(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_remove_point(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_add_input(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);
void 	rtneural_add_output(t_rtneural *x, t_symbol *s, int argc, t_atom *argv);


// globals
//static t_class	*s_collect_class = NULL;
static t_class *rtneural_class = NULL;

/************************************************************************************/

void ext_main(void *r)
{
	  t_class	*c = class_new("rtneural",
        (method)rtneural_new,
        (method)rtneural_free, sizeof(t_rtneural),
        (method)NULL,
        A_GIMME, 0);

	class_addmethod(c, (method)rtneural_bang,"bang",0);
	class_addmethod(c, (method)rtneural_list,	"list",	A_GIMME,0);
  class_addmethod(c, (method)rtneural_load_model, "load_model", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_write_json, "write_json", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_read_json, "read_json", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_bypass, "bypass", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_set_epochs, "set_epochs", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_set_layers_data, "set_layers_data", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_set_learn_rate, "set_learn_rate", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_clear_points, "clear_points", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_post_points, "post_points", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_add_input, "add_input", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_add_output, "add_output", A_GIMME, 0);
  class_addmethod(c, (method)rtneural_remove_point, "remove_point", A_GIMME, 0);
	class_register(CLASS_BOX, c);
	rtneural_class = c;
}


/************************************************************************************/
// object Creation Method

void* rtneural_new(t_symbol *s, long argc, t_atom *argv) {  

	t_rtneural *x;
	x = (t_rtneural *)object_alloc(rtneural_class);

	if (x) {
		//systhread_mutex_new(&x->c_mutex, 0);
		x->outlet = outlet_new(x, NULL);

		float n_in_chans = atom_getfloat(argv);
		float n_out_chans = atom_getfloat(argv+1);

    x->epochs = 2000;
    x->learn_rate = 0.001;

    x->in_vals.clear();
    x->out_vals.clear();
    x->layers_ints.clear();
    x->layers_strings.clear();

    x->layers_ints.push_back(5);
    x->layers_strings.push_back("relu");
    x->layers_ints.push_back(10);
    x->layers_strings.push_back("sigmoid");

		if(n_in_chans<1.f){
			n_in_chans = 1.f;
		}
		if(n_out_chans<1.f){
			n_out_chans = 1.f;
		}
		x->n_in_chans = t_int(n_in_chans);
		x->n_out_chans = t_int(n_out_chans);
		x->out_list = (t_atom *)getbytes(n_out_chans * sizeof(t_atom));

		x->bypass = 0;
		x->model_loaded = 0.f;

		x->input_to_nn = (float*)sysmem_newptr(x->n_in_chans*sizeof(float));
		x->output_from_nn = (float*)sysmem_newptr(x->n_out_chans*sizeof(float));

		x->processor.initialize(x->n_in_chans, x->n_out_chans, x->ratio);
	}
  return x;
}

void rtneural_set_epochs(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  x->epochs = t_int(atom_getfloat(argv));
}

void rtneural_set_layers_data(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  x->layers_ints.clear();
  x->layers_strings.clear();
  for (int i = 0; i < argc / 2; i++) {
    x->layers_ints.push_back(atom_getfloat(argv + i * 2));
    x->layers_strings.push_back(atom_getsym(argv + i * 2 + 1)->s_name);
  }
}

void rtneural_set_learn_rate(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  x->learn_rate = atom_getfloat(argv);
}

void rtneural_clear_points(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  x->in_vals.clear();
  x->out_vals.clear();
  post("cleared in and out vals");
}

void rtneural_post_points(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  post("inputs:");
  std::string temp_str;
  for (size_t i = 0; i < x->in_vals.size(); i++) {
    temp_str.clear();
    temp_str += "point ";
    temp_str += std::to_string(i);
    temp_str += " ";
    for (size_t j = 0; j < x->in_vals[i].size(); j++) {
      temp_str += std::to_string(x->in_vals[i][j]);
      if (j < x->in_vals[i].size() - 1) {
        temp_str += ", ";
      }
    }
    post(temp_str.c_str());
  }
  post("outputs:");
  for (size_t i = 0; i < x->out_vals.size(); i++) {
    temp_str.clear();
    temp_str += "point ";
    temp_str += std::to_string(i);
    temp_str += " ";
    for (size_t j = 0; j < x->out_vals[i].size(); j++) {
      temp_str += std::to_string(x->out_vals[i][j]);
      if (j < x->out_vals[i].size() - 1) {
        temp_str += ", ";
      }
    }
    post(temp_str.c_str());
  }
}

void rtneural_remove_point(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  t_int index = t_int(atom_getfloat(argv));
  if (index < 0 || index >= x->in_vals.size()) {
    post("index out of range");
    return;
  }
  x->in_vals.erase(x->in_vals.begin() + index);
  x->out_vals.erase(x->out_vals.begin() + index);
}

void rtneural_add_input(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  std::vector<float> in_temp;
  for (int i = 0; i < argc; i++) {
    in_temp.push_back(atom_getfloat(argv + i));
  }
  x->in_vals.push_back(in_temp);
}

void rtneural_add_output(t_rtneural *x, t_symbol *s, int argc, t_atom *argv) {
  std::vector<float> out_temp;
  for (int i = 0; i < argc; i++) {
    out_temp.push_back(atom_getfloat(argv + i));
  }
  x->out_vals.push_back(out_temp);
}

std::string get_abs_path(t_rtneural *x, std::string filename_in){
  t_object *jp;
    t_max_err err = object_obex_lookup(x, gensym("#P"), (t_object **)&jp);
    if (err != MAX_ERR_NONE){
      post("Error getting parent patcher");
      return "ERROR";
    }
    t_symbol *path = object_attr_getsym(jp, gensym("filepath"));

    std::string parent_directory = path->s_name;
    size_t pos = parent_directory.find_last_of("/\\");
    if (pos != std::string::npos) {
      parent_directory = parent_directory.substr(0, pos);
    }
    parent_directory = parent_directory.substr(13);
    if (!std::filesystem::is_directory(parent_directory.c_str())) {
      post("The directory does not exist or is not a directory");
      return "ERROR";
    }
    return (parent_directory +"/"+ filename_in);
}

void rtneural_read_json(t_rtneural *x, t_symbol s, long argc, t_atom *argv){
  

  t_symbol* path_in = atom_getsym(argv);
  std::string filename_in = path_in->s_name;
  size_t pos = filename_in.find_first_of("/\\");
  std::string filename;
  if(pos==0)
  {
    filename = filename_in;
  } else {
    filename = get_abs_path(x, filename_in);
    if(filename=="ERROR"){
      return;
    }
  }

  post("loading json: ");
  post(filename.c_str());

  nlohmann::json data = nlohmann::json::object();
  std::ifstream input_file(filename);
  if (!input_file.is_open()) {
    post("Failed to open input file");
    return;
  }

  try {
    input_file >> data;
    input_file.close();
  } catch (const std::exception &e) {
    post("Error reading JSON file: %s", e.what());
    return;
  }

  x->epochs = data["epochs"].get<int>();
  x->learn_rate = data["learn_rate"].get<float>();

  x->layers_ints.clear();
  x->layers_strings.clear();
  for (const auto& layer : data["layers_data"]) {
    x->layers_ints.push_back(layer[0].get<int>());
    x->layers_strings.push_back(layer[1].get<std::string>());
  }

  x->in_vals.clear();
  for (const auto& in_val : data["in_vals"]) {
    std::vector<float> temp;
    for (const auto& val : in_val) {
      temp.push_back(val.get<float>());
    }
    x->in_vals.push_back(temp);
  }

  x->out_vals.clear();
  for (const auto& out_val : data["out_vals"]) {
    std::vector<float> temp;
    for (const auto& val : out_val) {
      temp.push_back(val.get<float>());
    }
    x->out_vals.push_back(temp);
  }

  post("JSON file loaded successfully");

}

void rtneural_write_json(t_rtneural *x, t_symbol s, long argc, t_atom *argv){
  nlohmann::json data;

  data["epochs"] = x->epochs;
  data["learn_rate"] = x->learn_rate;

  data["layers_data"] = nlohmann::json::array_t();
  for (size_t i = 0; i < x->layers_ints.size(); i++) {
    data["layers_data"].push_back({x->layers_ints[i], x->layers_strings[i]});
  }

  data["in_vals"] = nlohmann::json::array_t();
  for(size_t i=0; i<x->in_vals.size(); i++){
    data["in_vals"].push_back(nlohmann::json::array_t());
    for(size_t j=0; j<x->in_vals[i].size(); j++){
      data["in_vals"][i].push_back(x->in_vals[i][j]);
    }
  }

  data["out_vals"] = nlohmann::json::array_t();
  for(size_t i=0; i<x->out_vals.size(); i++){
    data["out_vals"].push_back(nlohmann::json::array_t());
    for(size_t j=0; j<x->out_vals[i].size(); j++){
      data["out_vals"][i].push_back(x->out_vals[i][j]);
    }
  }

  t_symbol* path_in = atom_getsym(argv);
  std::string filename_in = path_in->s_name;
  size_t pos = filename_in.find_first_of("/\\");
  std::string filename;
  if(pos==0)
  {
    filename = filename_in;
  } else {
    filename = get_abs_path(x, filename_in);
    if(filename=="ERROR"){
      return;
    }
  }

  post(filename.c_str());

  std::ofstream output_file(filename);
  if (!output_file.is_open())  {
    post("Failed to open output file");
  } else {
    output_file << data;
    output_file.close();
    post("writing output file");
  }
}

void rtneural_free (t_rtneural* x) {
  sysmem_freeptr(x->input_to_nn);
  sysmem_freeptr(x->output_from_nn);
}

void rtneural_bang(t_rtneural *x)
{
    t_symbol *s = gensym("list");
    outlet_list(x->outlet, s, x->n_out_chans, x->out_list);
}

void rtneural_list(t_rtneural *x, t_symbol *s, long argc, t_atom *argv) {
    if ((x->processor.m_model_loaded==0)||((t_int)x->bypass==1)) {
        return;
    }
    for(int i=0; i<x->n_in_chans; i++){
        x->input_to_nn[i] = atom_getfloat(argv+i);
    }
    x->processor.process1(x->input_to_nn, x->output_from_nn);
    for(int i=0; i<x->n_out_chans; i++){
        atom_setfloat(x->out_list+i, x->output_from_nn[i]);
    }
    outlet_list(x->outlet, NULL, x->n_out_chans, x->out_list);
}

void rtneural_load_model(t_rtneural *x, t_symbol s, long argc, t_atom *argv){
  (void)x;

  t_symbol* path = atom_getsym(argv);
  std::string filename_in = path->s_name;
  size_t pos = filename_in.find_first_of("/\\");
  std::string filename;
  if(pos==0)
  {
    filename = filename_in;
  } else {
    filename = get_abs_path(x, filename_in);
  }

  post("loading model: ");
  post(filename.c_str());

  t_int test = x->processor.load_model(filename, 1);
  if(test==1){
    x->model_loaded = 1;

    post("model input size: %i", x->processor.m_model_input_size);
    post("model output size: %i", x->processor.m_model_output_size);
  } 
  else {
    x->model_loaded = 0;
    switch(test){
      case 0:
        post("error loading the model");
        break;
      case 2:
        post("error: model input size does not match the number of input channels");
        break;
      case 3:
        post("error: model output size does not match the number of output channels");
        break;
      default:
        post("error: the path does not exist or is not a file");
        break;
    }
    post("disabling model");
  }
}  

void rtneural_bypass(t_rtneural *x, long f){
  x->bypass = f;

  post(f ? "Bypass ON" : "Bypass OFF");
}  

/************************************************************************************/
