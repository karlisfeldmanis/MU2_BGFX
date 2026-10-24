// A small JSON tree for the Armory's dump: objects keep their keys sorted, numbers print the same
// on every run, so one release's files diff cleanly against the last's.
#pragma once

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace armory {

class J {
public:
    enum class T : uint8_t { Null, Bool, Int, Real, Str, Arr, Obj };

    J() = default;
    J(std::nullptr_t) {}
    J(bool b) : t_(T::Bool), b_(b) {}
    J(int v) : t_(T::Int), i_(v) {}
    J(unsigned v) : t_(T::Int), i_(int64_t(v)) {}
    J(long v) : t_(T::Int), i_(v) {}
    J(long long v) : t_(T::Int), i_(v) {}
    J(unsigned long v) : t_(T::Int), i_(int64_t(v)) {}
    J(unsigned long long v) : t_(T::Int), i_(int64_t(v)) {}
    J(double v) : t_(T::Real), d_(v) {}
    J(float v) : t_(T::Real), d_(double(v)), digits_(7) {}
    J(const char* s) : t_(s ? T::Str : T::Null), s_(s ? s : "") {}
    J(const std::string& s) : t_(T::Str), s_(s) {}
    J(std::string&& s) : t_(T::Str), s_(std::move(s)) {}

    static J arr() { J j; j.t_ = T::Arr; return j; }
    static J obj() { J j; j.t_ = T::Obj; return j; }
    // A probability or a share, to six significant digits.
    static J p(double v) { J j(v); j.digits_ = 6; return j; }

    T type() const { return t_; }
    bool null() const { return t_ == T::Null; }

    // An object's member, made on first use.
    J& operator[](const std::string& key) {
        if (t_ != T::Obj) { t_ = T::Obj; }
        return o_[key];
    }
    // Chained setting: o.set("a", 1).set("b", 2).
    J& set(const std::string& key, J value) {
        (*this)[key] = std::move(value);
        return *this;
    }
    J& push(J value) {
        if (t_ != T::Arr) t_ = T::Arr;
        a_.push_back(std::move(value));
        return a_.back();
    }
    size_t size() const { return t_ == T::Arr ? a_.size() : t_ == T::Obj ? o_.size() : 0; }
    bool has(const std::string& key) const { return t_ == T::Obj && o_.count(key) != 0; }
    const J* find(const std::string& key) const {
        if (t_ != T::Obj) return nullptr;
        auto it = o_.find(key);
        return it == o_.end() ? nullptr : &it->second;
    }
    bool boolean() const { return t_ == T::Bool && b_; }
    double real() const { return t_ == T::Real ? d_ : t_ == T::Int ? double(i_) : 0.0; }
    const std::string& str() const { return s_; }
    std::map<std::string, J>& members() { return o_; }
    const std::map<std::string, J>& members() const { return o_; }
    std::vector<J>& items() { return a_; }
    const std::vector<J>& items() const { return a_; }

    // Written with one-space indents, scalars and short arrays of scalars on one line.
    void write(std::string& out, int depth = 0) const;
    bool save(const std::string& path) const;

private:
    T t_ = T::Null;
    bool b_ = false;
    int64_t i_ = 0;
    double d_ = 0.0;
    int digits_ = 10;
    std::string s_;
    std::vector<J> a_;
    std::map<std::string, J> o_;

    bool flat() const;
};

void quote(std::string& out, const std::string& s);

}  // namespace armory
