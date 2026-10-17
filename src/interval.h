#pragma once

#include <cmath>
#include <limits>

class interval {
  public:
    double min, max;

    // default is empty
    interval() : min(+std::numeric_limits<double>::infinity()), max(-std::numeric_limits<double>::infinity()) {}

    interval(double min, double max) : min(min), max(max) {}

    interval(const interval& a, const interval& b)
        : min(std::fmin(a.min, b.min)), max(std::fmax(a.max, b.max)) {}

    double size() const {
        return max - min;
    }

    interval expand(double delta) const {
        double padding = delta / 2;
        return interval(min - padding, max + padding);
    }

    bool contains(double x) const {
        return min <= x && x <= max;
    }

    bool surrounds(double x) const {
        return min < x && x < max;
    }
};
