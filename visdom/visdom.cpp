/*
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * Most of this code is a pretty straight clone of the Visdom Python
 * implementation at
 * https://github.com/facebookresearch/visdom/blob/master/py/__init__.py
 */

#include "visdom.h"

#include <curl/curl.h>
#include <glog/logging.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <torch/torch.h>

#include <mutex>

namespace rj = rapidjson;

namespace visdom {

namespace {
template <typename T, typename Allocator>
rj::Value rjvalue(T const& val, Allocator&&) {
  return rj::Value(val);
}

template <typename Allocator>
rj::Value rjvalue(std::string const& str, Allocator&& alloc) {
  rj::Value value;
  value.SetString(str.c_str(), str.size(), alloc);
  return value;
}

template <typename Allocator>
rj::Value rjvalue(StringList const& slist, Allocator&& alloc) {
  rj::Value value(rj::kArrayType);
  for (auto const& s : slist) {
    value.PushBack(rjvalue(s, alloc).Move(), alloc);
  }
  return value;
}

template <typename Allocator>
rj::Value rjvalue(StringListMap const& slmap, Allocator&& alloc) {
  rj::Value value(rj::kObjectType);
  for (auto const& it : slmap) {
    auto keyval = rjvalue(it.first, alloc);
    value.AddMember(keyval, rjvalue(it.second, alloc).Move(), alloc);
  }
  return value;
}

template <typename T, typename Allocator>
rj::Value rjtensor(
    torch::TensorAccessor<T, 1> acc,
    Allocator&& alloc,
    bool nanAsNull = false) {
  rj::Value value(rj::kArrayType);
  if (!nanAsNull) {
    for (int64_t i = 0; i < acc.size(0); i++) {
      value.PushBack(acc[i], alloc);
    }
  } else {
    for (int64_t i = 0; i < acc.size(0); i++) {
      if (std::isnan(acc[i])) {
        value.PushBack(rj::Value().Move(), alloc);
      } else {
        value.PushBack(acc[i], alloc);
      }
    }
  }
  return value;
}

template <typename T, size_t N, typename Allocator>
rj::Value rjtensor(
    torch::TensorAccessor<T, N> acc,
    Allocator&& alloc,
    bool nanAsNull = false) {
  rj::Value value(rj::kArrayType);
  for (int64_t i = 0; i < acc.size(0); i++) {
    value.PushBack(rjtensor(acc[i], alloc, nanAsNull).Move(), alloc);
  }
  return value;
}

template <typename Allocator>
rj::Value
rjvalue(torch::Tensor tensor, Allocator&& alloc, bool nanAsNull = false) {
  switch (tensor.type().scalarType()) {
    case torch::kFloat: {
      switch (tensor.dim()) {
        case 1:
          return rjtensor(tensor.accessor<float, 1>(), alloc, nanAsNull);
        case 2:
          return rjtensor(tensor.accessor<float, 2>(), alloc, nanAsNull);
        case 3:
          return rjtensor(tensor.accessor<float, 3>(), alloc, nanAsNull);
        default:
          throw std::runtime_error(
              "Cannot handle tensor with more than 3 dimensions");
      }
    }
    case torch::kDouble:
      switch (tensor.dim()) {
        case 1:
          return rjtensor(tensor.accessor<double, 1>(), alloc, nanAsNull);
        case 2:
          return rjtensor(tensor.accessor<double, 2>(), alloc, nanAsNull);
        case 3:
          return rjtensor(tensor.accessor<double, 3>(), alloc, nanAsNull);
        default:
          throw std::runtime_error(
              "Cannot handle tensor with more than 3 dimensions");
      }
    case torch::kInt:
      switch (tensor.dim()) {
        case 1:
          return rjtensor(tensor.accessor<int, 1>(), alloc, nanAsNull);
        case 2:
          return rjtensor(tensor.accessor<int, 2>(), alloc, nanAsNull);
        case 3:
          return rjtensor(tensor.accessor<int, 3>(), alloc, nanAsNull);
        default:
          throw std::runtime_error(
              "Cannot handle tensor with more than 3 dimensions");
      }
    default:
      throw std::runtime_error("Cannot handle tensor type");
  }
}

template <typename T>
T optget(Options const& opts, std::string const& key, T defaultValue) {
  auto it = opts.find(key);
  if (it != opts.end()) {
    return it->second.get<T>();
  }
  return defaultValue;
}

template <typename T, typename Allocator>
void objAddMember(
    rj::Value& obj,
    Options const& opts,
    std::string const& optkey,
    Allocator&& alloc,
    std::string const& key = std::string()) {
  auto it = opts.find(optkey);
  if (it != opts.end()) {
    obj.AddMember(
        rjvalue(!key.empty() ? key : optkey, alloc).Move(),
        rjvalue(it->second.get<T>(), alloc).Move(),
        alloc);
  }
}

template <typename T, typename Allocator>
void objAddMember(
    rj::Value& obj,
    Options const& opts,
    std::string const& optkey,
    T defaultValue,
    Allocator&& alloc,
    std::string const& key = std::string()) {
  auto it = opts.find(optkey);
  auto keyval = rjvalue(!key.empty() ? key : optkey, alloc);
  if (it != opts.end()) {
    obj.AddMember(keyval, rjvalue(it->second.get<T>(), alloc).Move(), alloc);
  } else {
    obj.AddMember(keyval, rjvalue(defaultValue, alloc).Move(), alloc);
  }
}

template <typename Allocator>
void objAddOptions(rj::Value& obj, Options const& opts, Allocator&& alloc) {
  rj::Value dest(rj::kObjectType);
  for (auto const& it : opts) {
    auto keyval = rjvalue(it.first, alloc);
    it.second.match(
        [&](bool v) {
          dest.AddMember(keyval, rjvalue(v, alloc).Move(), alloc);
        },
        [&](double v) {
          dest.AddMember(keyval, rjvalue(v, alloc).Move(), alloc);
        },
        [&](std::string v) {
          dest.AddMember(keyval, rjvalue(v, alloc).Move(), alloc);
        },
        [&](StringList v) {
          dest.AddMember(keyval, rjvalue(v, alloc).Move(), alloc);
        },
        [&](StringListMap v) {
          dest.AddMember(keyval, rjvalue(v, alloc).Move(), alloc);
        },
        [&](torch::Tensor v) {
          dest.AddMember(keyval, rjvalue(v, alloc).Move(), alloc);
        });
  }

  obj.AddMember("opts", dest.Move(), alloc);
}

template <typename Allocator>
void addAxisFormat(
    rj::Value& obj,
    Options const& opts,
    char const* optkey,
    Allocator&& alloc,
    char const* key = nullptr) {
  rj::Value fmt(rj::kObjectType);
  std::string prefix(optkey);
  objAddMember<std::string>(fmt, opts, prefix + "type", alloc, "type");
  objAddMember<std::string>(fmt, opts, prefix + "title", alloc, "title");
  auto itmin = opts.find(prefix + "tickmin");
  auto itmax = opts.find(prefix + "tickmax");
  if (itmin != opts.end() && itmax != opts.end()) {
    rj::Value range(rj::kArrayType);
    range.PushBack(itmin->second.get<double>(), alloc);
    range.PushBack(itmax->second.get<double>(), alloc);
    fmt.AddMember("range", range, alloc);
  }
  objAddMember<double>(fmt, opts, prefix + "tickstep", alloc, "tickwidth");
  objAddMember<bool>(fmt, opts, prefix + "ytick", alloc, "showticklabels");

  if (!fmt.ObjectEmpty()) {
    auto const& okey = key ? key : optkey;
    obj.AddMember(rj::StringRef(okey), fmt, alloc);
  }
}

template <typename Allocator>
rj::Value
layoutObject(Options const& opts, Allocator&& alloc, bool is3d = false) {
  rj::Value obj(rj::kObjectType);
  objAddMember<double>(obj, opts, "width", alloc);
  objAddMember<double>(obj, opts, "height", alloc);
  auto it = opts.find("legend");
  if (it != opts.end()) {
    if (it->second.is<bool>()) {
      obj.AddMember("showlegend", it->second.get<bool>(), alloc);
    } else {
      obj.AddMember("showlegend", true, alloc);
    }
  } else {
    obj.AddMember("showlegend", false, alloc);
  }
  objAddMember<std::string>(obj, opts, "title", alloc, "title");
  addAxisFormat(obj, opts, "x", alloc, "xaxis");
  addAxisFormat(obj, opts, "y", alloc, "yaxis");

  rj::Value margin(rj::kObjectType);
  objAddMember<double>(obj, opts, "marginleft", 60, alloc, "l");
  objAddMember<double>(obj, opts, "marginright", 60, alloc, "r");
  objAddMember<double>(obj, opts, "margintop", 60, alloc, "t");
  objAddMember<double>(obj, opts, "marginbottom", 60, alloc, "b");

  if (is3d) {
    addAxisFormat(obj, opts, "z", alloc, "zaxis");
  }

  it = opts.find("stacked");
  if (it != opts.end()) {
    obj.AddMember("barmode", it->second.get<bool>() ? "stack" : "group", alloc);
  }

  return obj;
}

void checkOpts(Options const& opts) {
  auto it = opts.find("color");
  if (it != opts.end() && !it->second.is<std::string>()) {
    throw std::runtime_error("color should be a string");
  }
  it = opts.find("colormap");
  if (it != opts.end() && !it->second.is<std::string>()) {
    throw std::runtime_error("colormap should be a string");
  }
  it = opts.find("mode");
  if (it != opts.end() && !it->second.is<std::string>()) {
    throw std::runtime_error("mode should be a string");
  }
  it = opts.find("markersymbol");
  if (it != opts.end() && !it->second.is<std::string>()) {
    throw std::runtime_error("marker symbol should be a string");
  }
  it = opts.find("markersize");
  if (it != opts.end() &&
      (!it->second.is<double>() || it->second.get<double>() <= 0.0f)) {
    throw std::runtime_error("marker size should be a positive number");
  }
  it = opts.find("columnnames");
  if (it != opts.end() && !it->second.is<StringList>()) {
    throw std::runtime_error("columnnames should be a vector of strings");
  }
  it = opts.find("rownames");
  if (it != opts.end() && !it->second.is<StringList>()) {
    throw std::runtime_error("rownames should be a vector of strings");
  }
  it = opts.find("jpgquality");
  if (it != opts.end() && !it->second.is<double>()) {
    throw std::runtime_error("JPG quality should be a number");
  }
  if (it != opts.end() &&
      (it->second.get<double>() <= 0 || it->second.get<double>() > 100)) {
    throw std::runtime_error(
        "JPG quality should be a number between 0 and 100");
  }
  it = opts.find("opacity");
  if (it != opts.end() && !it->second.is<double>()) {
    throw std::runtime_error("opacity should be a number");
  }
  if (it != opts.end() &&
      (it->second.get<double>() < 0 || it->second.get<double>() > 1)) {
    throw std::runtime_error("opacity should be a number between 0 and 1");
  }
}

OptionValue markerColorCheck(
    OptionValue const& val,
    torch::Tensor X,
    torch::Tensor Y,
    int64_t L) {
  if (!val.is<torch::Tensor>()) {
    throw std::runtime_error("maker color should be a tensor");
  }
  auto mc = val.get<torch::Tensor>();
  if (!(mc.size(0) == L || ((mc.size(0) == X.size(0) && mc.dim()) == 1 ||
                            (mc.dim() == 2 && mc.size(1) == 3)))) {
    std::ostringstream msg;
    msg << "marker colors have to be of size `" << X.size(0) << "` or `"
        << X.size(1) << " x 3` or `" << L << "` or `" << L << " x 3` but got: ";
    for (int64_t i = 0; i < mc.dim(); i++) {
      if (i > 0) {
        msg << "x";
      }
      msg << mc.size(i);
    }
    auto s = msg.str();
    throw std::runtime_error(s.c_str());
  }

  if (!mc.ge(0).all().item<int32_t>()) {
    throw std::runtime_error("marker colors have to be >= 0");
  }
  if (!mc.le(255).all().item<int32_t>()) {
    throw std::runtime_error("marker colors have to be <= 255");
  }
  if (!torch::isIntegralType(mc.type().scalarType()) && !mc.equal(mc.floor())) {
    throw std::runtime_error("marker colors are assumed to be ints");
  }

  mc = mc.to(torch::kCPU).toType(torch::kByte);
  StringList markercolor;
  if (mc.dim() == 1) {
    // TODO: Original implementation assumes grayscale or what?
    char buf[8];
    auto p = mc.accessor<uint8_t, 1>();
    for (int64_t i = 0; i < p.size(0); i++) {
      snprintf(buf, sizeof(buf), "#%02x%02x%02x", p[i], p[i], p[i]);
      markercolor.push_back(buf);
    }
  } else {
    char buf[8];
    auto p = mc.accessor<uint8_t, 2>();
    for (int64_t i = 0; i < p.size(0); i++) {
      snprintf(buf, sizeof(buf), "#%02x%02x%02x", p[i][0], p[i][1], p[i][2]);
      markercolor.push_back(buf);
    }
  }

  if (mc.size(0) != X.size(0)) {
    StringList tmp;
    for (int64_t i = 0; i < Y.size(0); i++) {
      tmp.push_back(markercolor[int(Y[i].item<double>()) - 1]);
    }
    std::swap(markercolor, tmp);
  }

  StringListMap ret;
  for (auto i = 0U; i < markercolor.size(); i++) {
    ret[static_cast<int>(Y[i].item<double>())].push_back(markercolor[i]);
  }
  return ret;
}


size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  std::string* dest = reinterpret_cast<std::string*>(userdata);
  dest->append(ptr, size * nmemb);
  return size * nmemb;
}
} // namespace

class VisdomImpl {
 public:
  ConnectionParams cparams_;
  std::string env_;
  bool send_;
  std::mutex mutex_;

  CURL* curl_;

  VisdomImpl(ConnectionParams cparams, std::string env, bool send)
      : cparams_(std::move(cparams)), env_(std::move(env)), send_(send) {
    curl_ = curl_easy_init();
  }

  ~VisdomImpl() {
    curl_easy_cleanup(curl_);
  }

  std::string send(
      rapidjson::Document& msg,
      std::string const& endpoint = "events") {
    std::unique_lock<std::mutex> lock(mutex_);

    if (!msg.HasMember("eid")) {
      msg.AddMember("eid", env_, msg.GetAllocator());
    }

    rj::StringBuffer buffer;
    rj::Writer<rj::StringBuffer> writer(buffer);
    msg.Accept(writer);
    VLOG(2) << buffer.GetString();

    std::string reply;
    auto url =
        cparams_.server + ":" + std::to_string(cparams_.port) + "/" + endpoint;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_POST, 1);
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, long(buffer.GetSize()));
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, buffer.GetString());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, reinterpret_cast<void*>(&reply));

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, errbuf);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
      VLOG(1) << errbuf;
      return std::string();
    }
    VLOG(2) << reply;
    return reply;
  }
};

Visdom::Visdom(ConnectionParams cparams, std::string env, bool send)
    : d_(new VisdomImpl(std::move(cparams), std::move(env), send)) {}

Visdom::~Visdom() {
  delete d_;
}

std::string Visdom::save(std::vector<std::string> const& envs) {
  rj::Document msg(rj::kObjectType);
  auto& alloc = msg.GetAllocator();

  rj::Value data(rj::kArrayType);
  for (auto const& env : envs) {
    data.PushBack(rjvalue(env, alloc), alloc);
  }

  msg.AddMember("data", data, alloc);
  return d_->send(msg, "save");
}

std::string Visdom::close(std::string const& win, std::string const& env) {
  rj::Document msg(rj::kObjectType);
  auto& alloc = msg.GetAllocator();
  if (!win.empty()) {
    msg.AddMember("win", win, alloc);
  }
  if (!env.empty()) {
    msg.AddMember("eid", env, alloc);
  }

  return d_->send(msg, "close");
}

std::string Visdom::text(
    std::string const& txt,
    std::string const& win,
    std::string const& env,
    Options const& opts) {
  rj::Document msg(rj::kObjectType);
  auto& alloc = msg.GetAllocator();

  rj::Value entry(rj::kObjectType);
  entry.AddMember("content", txt, alloc);
  entry.AddMember("type", "text", alloc);

  rj::Value data(rj::kArrayType);
  data.PushBack(entry, alloc);

  msg.AddMember("data", data, alloc);
  if (!win.empty()) {
    msg.AddMember("win", win, alloc);
  }
  if (!env.empty()) {
    msg.AddMember("eid", env, alloc);
  }
  objAddOptions(msg, opts, alloc);
  return d_->send(msg);
}

std::string Visdom::heatmap(
    torch::Tensor tensor,
    std::string const& win,
    std::string const& env,
    Options const& opts) {
  if (tensor.dim() != 2) {
    throw std::runtime_error("data should be two-dimensional");
  }

  Options defopts = opts;
  defopts["xmin"] = optget(opts, "xmin", tensor.min().item<double>());
  defopts["xmax"] = optget(opts, "xmax", tensor.max().item<double>());
  defopts["colormap"] = optget(opts, "colormap", std::string("Viridis"));
  checkOpts(opts);

  auto it = defopts.find("columnnames");
  if (it != defopts.end() &&
      int64_t(it->second.get<StringList>().size()) != tensor.size(1)) {
    throw std::runtime_error(
        "number of column names should match number of columns in X");
  }
  it = defopts.find("rownames");
  if (it != defopts.end() &&
      int64_t(it->second.get<StringList>().size()) != tensor.size(0)) {
    throw std::runtime_error(
        "number of column names should match number of columns in X");
  }

  rj::Document msg(rj::kObjectType);
  auto& alloc = msg.GetAllocator();

  rj::Value entry(rj::kObjectType);
  entry.AddMember("z", rjvalue(tensor, alloc).Move(), alloc);
  objAddMember<StringList>(entry, defopts, "columnnames", alloc, "x");
  objAddMember<StringList>(entry, defopts, "rownames", alloc, "y");
  entry.AddMember("zmin", defopts["xmin"].get<double>(), alloc);
  entry.AddMember("zmax", defopts["xmax"].get<double>(), alloc);
  entry.AddMember("type", "heatmap", alloc);
  entry.AddMember("colorscale", defopts["colormap"].get<std::string>(), alloc);

  rj::Value data(rj::kArrayType);
  data.PushBack(entry, alloc);

  msg.AddMember("data", data, alloc);
  if (!win.empty()) {
    msg.AddMember("win", win, alloc);
  }
  if (!env.empty()) {
    msg.AddMember("eid", env, alloc);
  }
  objAddOptions(msg, opts, alloc);
  msg.AddMember("layout", layoutObject(defopts, alloc), alloc);
  return d_->send(msg);
}

std::string Visdom::scatter(
    torch::Tensor X,
    torch::Tensor Y,
    std::string const& win,
    std::string const& env,
    std::string const& name,
    Options const& opts,
    UpdateMethod update) {
  if (update == UpdateMethod::Remove) {
    if (win.empty()) {
      throw std::runtime_error("A window must be specified for deletion");
    }
    if (name.empty()) {
      throw std::runtime_error("A trace must be specified for deletion");
    }
    if (!opts.empty()) {
      throw std::runtime_error("Opts cannot be updated on trace deletion");
    }

    rj::Document msg(rj::kObjectType);
    auto& alloc = msg.GetAllocator();
    msg.AddMember("data", rj::Value(rj::kArrayType), alloc);
    msg.AddMember("name", name, alloc);
    msg.AddMember("delete", true, alloc);
    msg.AddMember("win", win, alloc);
    msg.AddMember("env", env, alloc);
    return d_->send(msg, "update");
  } else if (update != UpdateMethod::None) {
    if (win.empty()) {
      throw std::runtime_error("A window must be specified for updates");
    }

    // Case when X is 1 dimensional and corresponding values on y-axis
    // are passed in parameter Y
    if (!name.empty()) {
      if (X.dim() != 1 && X.dim() != 2) {
        throw std::runtime_error(
            "Updating by name should have 1-dim or 2-dim X.");
      }
      if (X.dim() == 1) {
        if (Y.dim() != 1) {
          throw std::runtime_error(
              "Update by name should have 1-dim Y when X is 1-dim");
        }
        if (X.size(0) != Y.size(0)) {
          throw std::runtime_error("X and Y should be the same shape");
        }
        X = torch::stack({X, Y}).t();
        Y = torch::Tensor();
      }
    }
  }

  if (X.dim() != 2) {
    throw std::runtime_error("X should have two dims");
  }
  if (X.size(1) != 2 && X.size(1) != 3) {
    throw std::runtime_error("X should have 2 or 3 cols");
  }

  if (Y.defined()) {
    Y = Y.squeeze();
    if (Y.dim() == 0) {
      Y = Y.unsqueeze(0);
    }
    if (Y.dim() != 1) {
      throw std::runtime_error("Y should be one-dimensional");
    }
    if (X.size(0) != Y.size(0)) {
      throw std::runtime_error("sizes of X and Y should match");
    }
  } else {
    Y = torch::ones(X.size(0), X.options());
  }

  if (!torch::isIntegralType(Y.type().scalarType()) && !Y.equal(Y.floor())) {
    throw std::runtime_error("labels should be integers");
  }
  if (Y.min().item<double>() != 1.0) {
    throw std::runtime_error("labels are assumed to be between 1 and K");
  }

  int64_t K = static_cast<int64_t>(Y.max().item<double>());
  bool is3d = X.size(1) == 3;

  Options defopts = opts;
  defopts["colormap"] = optget(opts, "colormap", std::string("Viridis"));
  defopts["mode"] = optget(opts, "mode", std::string("markers"));
  defopts["markersymbol"] = optget(opts, "markersymbol", std::string("dot"));
  defopts["borderwidth"] = optget(opts, "borderwidth", 0.5);
  defopts["markersize"] = optget(opts, "markersize", 10.0);
  if (defopts.find("markercolor") != defopts.end()) {
    defopts["markercolor"] = markerColorCheck(defopts["markercolor"], X, Y, K);
  }
  checkOpts(opts);

  StringList legend;
  if (defopts.find("legend") != defopts.end()) {
    if (!defopts["legend"].is<StringList>()) {
      throw std::runtime_error("legend should be a vector of strings");
    }
    legend = defopts["legend"].get<StringList>();
    if (static_cast<int64_t>(legend.size()) != K) {
      // TODO: better error message
      throw std::runtime_error("wrong size for legend");
    }
  }

  rj::Document msg(rj::kObjectType);
  auto& alloc = msg.GetAllocator();

  rj::Value data(rj::kArrayType);
  auto mcit = defopts.find("markercolor");
  for (int64_t k = 1; k <= K; k++) {
    auto ind = Y.eq(k);
    if (!ind.any().item<int32_t>()) {
      continue;
    }

    rj::Value _data(rj::kObjectType);
    _data.AddMember(
        "x", rjvalue(X.select(1, 0).masked_select(ind), alloc, true), alloc);
    _data.AddMember(
        "y", rjvalue(X.select(1, 1).masked_select(ind), alloc, true), alloc);

    if (!legend.empty() && !legend[k - 1].empty()) {
      _data.AddMember("name", rjvalue(legend[k - 1], alloc), alloc);
    } else {
      _data.AddMember("name", rjvalue(std::to_string(k), alloc), alloc);
    }
    if (is3d) {
      _data.AddMember("type", "scatter3d", alloc);
    } else {
      _data.AddMember("type", "scatter", alloc);
    }
    objAddMember<std::string>(_data, defopts, "mode", alloc);

    rj::Value marker(rj::kObjectType);
    objAddMember<double>(marker, defopts, "markersize", alloc, "size");
    objAddMember<std::string>(marker, defopts, "markersymbol", alloc, "symbol");
    if (mcit != defopts.end()) {
      marker.AddMember(
          "color", rjvalue(mcit->second.get<StringListMap>()[k], alloc), alloc);
    }

    rj::Value markerline(rj::kObjectType);
    markerline.AddMember("color", "#000000", alloc);
    objAddMember<double>(markerline, defopts, "borderwidth", alloc, "width");
    marker.AddMember("line", markerline, alloc);
    _data.AddMember("marker", marker, alloc);

    if (optget(defopts, "fillarea", false)) {
      _data.AddMember("fill", "tonexty", alloc);
    }
    if (is3d) {
      _data.AddMember(
          "y", rjvalue(X.select(1, 2).masked_select(ind), alloc, false), alloc);
    }

    data.PushBack(_data, alloc);
  }

  msg.AddMember("data", data, alloc);
  if (!win.empty()) {
    msg.AddMember("win", win, alloc);
  }
  if (!env.empty()) {
    msg.AddMember("eid", env, alloc);
  }
  objAddOptions(msg, opts, alloc);
  msg.AddMember("layout", layoutObject(defopts, alloc, is3d), alloc);

  if (update != UpdateMethod::None) {
    if (!name.empty()) {
      msg.AddMember("name", name, alloc);
    }
    msg.AddMember("append", update == UpdateMethod::Append, alloc);
    return d_->send(msg, "update");
  } else {
    return d_->send(msg);
  }
}

std::string Visdom::line(
    torch::Tensor Y,
    torch::Tensor X,
    std::string const& win,
    std::string const& env,
    std::string const& name,
    Options const& opts,
    UpdateMethod update) {
  if (update != UpdateMethod::None) {
    if (update == UpdateMethod::Remove) {
      return scatter(X, Y, win, env, name, opts, update);
    } else {
      if (!X.defined()) {
        throw std::runtime_error("must specify x-values for line updates");
      }
    }
  }

  if (!(Y.dim() == 1 || Y.dim() == 2)) {
    throw std::runtime_error("Y should have 1 or 2 dim");
  }

  if (X.defined()) {
    if (!(X.dim() == 1 || X.dim() == 2)) {
      throw std::runtime_error("X should have 1 or 2 dim");
    }
  } else {
    X = torch::linspace(0, 1, Y.size(0), torch::CPU(torch::kFloat));
  }

  if (Y.dim() == 2 && X.dim() == 1) {
    X = torch::stack(std::vector<torch::Tensor>(Y.size(1), X)).t();
  }

  if (X.sizes().vec() != Y.sizes().vec()) {
    throw std::runtime_error("X and Y should be the same shape");
  }

  Options defopts = opts;
  defopts["markers"] = optget(opts, "makers", false);
  defopts["fillarea"] = optget(opts, "fillarea", false);
  if (defopts["makers"].get<bool>()) {
    defopts["mode"] = std::string("lines+markers");
  } else {
    defopts["mode"] = std::string("lines");
  }
  checkOpts(defopts);

  torch::Tensor linedata;
  if (Y.dim() == 1) {
    linedata = torch::stack({X, Y}).t();
  } else {
    linedata =
        torch::stack(
            {X.t().contiguous().view({-1}), Y.t().contiguous().view({-1})})
            .t();
  }

  torch::Tensor labels;
  if (Y.dim() == 2) {
    labels = torch::arange(1, Y.size(1) + 1, 1, torch::CPU(torch::kInt));
    labels = torch::stack(std::vector<torch::Tensor>(Y.size(0), labels))
                 .t()
                 .contiguous()
                 .view(-1);
  }

  return scatter(linedata, labels, win, env, name, defopts, update);
}

} // namespace visdom
