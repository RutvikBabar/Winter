#include <winter/core/signal.hpp>

using namespace winter::core;

Signal::Signal()
    : symbol(""), type(SignalType::NEUTRAL), strength(0.0), price(0.0) {}

Signal::Signal(const std::string& sym, SignalType t, double s, double p)
    : symbol(sym), type(t), strength(s), price(p) {}
