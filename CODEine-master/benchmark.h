#ifndef I_BMRK_7K45G66H7JI
#define I_BMRK_7K45G66H7JI

#include <map>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <cstddef>
#include <utility>
#include <fstream>
#include <numeric>
#include <iostream>
#include <algorithm>
#include <type_traits>

#define fw(what) std::forward<decltype(what)>(what)

namespace bmk {

    using std::map;
    using std::pair;
    using std::vector;
    using std::string;
    using std::size_t;
    using std::ostream;
    using std::is_same;
    using std::forward;
    using std::ofstream;
    using std::enable_if;
    using std::unique_ptr;
    using std::result_of_t;
    using std::make_unique;
    using std::initializer_list;
    using std::remove_reference;
    using std::integral_constant;
    using std::chrono::time_point;
    using std::chrono::duration_cast;

/// folly function to avoid optimizing code away
#ifdef _MSC_VER
#pragma optimize("", off)
    template <class T>
    void doNotOptimizeAway(T&& datum)
    {
        datum = datum;
    }
#pragma optimize("", on)
#else

    template<class T>
    void doNotOptimizeAway(T &&datum) {
        asm volatile("" : "+r"(datum));
    }

#endif

    /// calculate the mean value
    template<typename C>
    auto mean(C &&data) {
        return std::accumulate(
                begin(data), end(data),
                (std::remove_reference_t<decltype(*begin(data))>) 0) /
               data.size();
    }

    /// get the name of the chrono time type
    template<typename T>
    inline string time_type() {
        return "unknown";
    }

    template<>
    inline string time_type<std::chrono::nanoseconds>() {
        return "nanoseconds";
    }

    template<>
    inline string time_type<std::chrono::microseconds>() {
        return "microseconds";
    }

    template<>
    inline string time_type<std::chrono::milliseconds>() {
        return "milliseconds";
    }

    template<>
    inline string time_type<std::chrono::seconds>() {
        return "seconds";
    }

    template<>
    inline string time_type<std::chrono::minutes>() {
        return "minutes";
    }

    template<>
    inline string time_type<std::chrono::hours>() {
        return "hours";
    }

    template<class TimeT  = std::chrono::milliseconds,
            class ClockT = std::chrono::steady_clock>
    class timeout {
        TimeT _total{0};
        decltype(ClockT::now()) _start;

    public:
        void tic() { _start = ClockT::now(); }

        void toc() { _total += duration_cast<TimeT>(ClockT::now() - _start); }

        TimeT duration() const { return _total; }
    };

    template<class TimeT  = std::chrono::milliseconds,
            class ClockT = std::chrono::steady_clock>
    using timeout_ptr      = unique_ptr<timeout<TimeT, ClockT>>;

    namespace detail {
        /**
        * @ class experiment_impl
        * @ brief implementation details of an experiment
        */
        template<class TimeT, class FactorT>
        struct experiment_impl {
            string _fctName;
            map<FactorT, vector<TimeT>> _timings;

            experiment_impl(string const &factorName) : _fctName(factorName) {}

            // implementation of forwarded functions --------------------
            void print(ostream &os) const {
                string token{""};

                os << ", 'factor_name' : '" << _fctName << "'";
                // print the factor list
                os << ", 'factors' : [ ";
                for (auto &&Pair : _timings) {
                    os << token;
                    os << Pair.first;
                    token = ", ";
                }
                os << " ]";
                // print the timings
                token.clear();
                os << ", 'timings' : [ ";
                for (auto &&Pair : _timings) {
                    os << token;
                    os << mean(Pair.second).count();
                    token = ", ";
                }
                os << " ]";
            }

        protected:
            ~experiment_impl() = default;
        };

        template<class TimeT>
        struct experiment_impl<TimeT, void> {
            vector<TimeT> _timings;

            experiment_impl(size_t nSample) : _timings(nSample) {}

            // implementation of forwarded functions --------------------
            void print(ostream &os) const {
                string token{""};
                // print the timings
                os << ", 'timings' : [ ";
                for (auto &&elem : _timings) {
                    os << token;
                    os << elem.count();
                    token = ", ";
                }
                os << " ]";
            }

        protected:
            ~experiment_impl() = default;
        };

        /**
        * @ class experiment
        * @ brief base class to serve as an experiment concept
        */
        struct experiment {
            virtual ~experiment() {}

            // forwarded functions --------------------------------------
            virtual void print(ostream &os) const = 0;
        };

        template<class TimeT, class ClockT>
        struct measure {
            template<class F>
            inline static auto duration(F &&callable) ->
            typename enable_if<!is_same<decltype(callable()),
                    timeout_ptr<TimeT, ClockT>>::value,
                    TimeT>::type {
                auto start = ClockT::now();
                fw(callable)();
                return duration_cast<TimeT>(ClockT::now() - start);
            }

            template<class F>
            inline static auto duration(F &&callable) ->
            typename enable_if<is_same<decltype(callable()),
                    timeout_ptr<TimeT, ClockT>>::value,
                    TimeT>::type {
                auto start = ClockT::now();
                auto tOut = fw(callable)();
                return (duration_cast<TimeT>(ClockT::now() - start)) -
                       tOut->duration();
            }

            template<class F, typename FactorT>
            inline static auto duration(F &&callable, FactorT &&factor) ->
            typename enable_if<
                    !is_same<decltype(callable(forward<FactorT>(factor))),
                            timeout_ptr<TimeT, ClockT>>::value,
                    TimeT>::type {
                auto start = ClockT::now();
                fw(callable)(forward<FactorT>(factor));
                return duration_cast<TimeT>(ClockT::now() - start);
            }

            template<class F, typename FactorT>
            inline static auto duration(F &&callable, FactorT &&factor) ->
            typename enable_if<
                    is_same<decltype(callable(forward<FactorT>(factor))),
                            timeout_ptr<TimeT, ClockT>>::value,
                    TimeT>::type {
                auto start = ClockT::now();
                auto tOut = fw(callable)(forward<FactorT>(factor));
                return (duration_cast<TimeT>(ClockT::now() - start)) -
                       tOut->duration();
            }
        };

        /**
        * @ class experiment_model
        * @ brief abrastraction for a single sampling process
        */
        template<class TimeT, class ClockT, class FactorT = void>
        struct experiment_model final : experiment,
                                        experiment_impl<TimeT, FactorT> {
            // construction - destruction -------------------------------
            template<class F>
            experiment_model(size_t nSample, F &&callable)
                    : experiment_impl<TimeT, void>(nSample) {
                for (size_t i = 0; i < nSample; i++) {
                    experiment_impl<TimeT, FactorT>::_timings[i] =
                            measure<TimeT, ClockT>::duration(fw(callable));
                }
            }

            template<class F>
            experiment_model(size_t nSample, F &&callable,
                             string const &factorName,
                             initializer_list<FactorT> &&factors)
                    : experiment_impl<TimeT, FactorT>(factorName) {
                for (auto &&factor : factors) {
                    experiment_impl<TimeT, FactorT>::_timings[factor].reserve(
                            nSample);
                    for (size_t i = 0; i < nSample; i++) {
                        experiment_impl<TimeT, FactorT>::_timings[factor]
                                .push_back(measure<TimeT, ClockT>::duration(
                                        fw(callable), factor));
                    }
                }
            }

            template<class F, class It>
            experiment_model(size_t nSample, F &&callable,
                             string const &factorName, It beg, It fin)
                    : experiment_impl<
                    TimeT, typename remove_reference<decltype(*beg)>::type>(
                    factorName) {
                while (beg != fin) {
                    experiment_impl<TimeT, FactorT>::_timings[*beg].reserve(
                            nSample);
                    for (size_t i = 0; i < nSample; i++) {
                        experiment_impl<TimeT, FactorT>::_timings[*beg]
                                .push_back(measure<TimeT, ClockT>::duration(
                                        fw(callable), *beg));
                    }
                    ++beg;
                }
            }

            // forwarded functions --------------------------------------
            void print(ostream &os) const override {
                experiment_impl<TimeT, FactorT>::print(os);
            }
        };
    } // ~ namespace detail

    /**
    * @ class benchmark
    * @ brief organizes the execution and serialization of timing experiments
    */
    template<class TimeT  = std::chrono::milliseconds,
            class ClockT = std::chrono::steady_clock>
    class benchmark {
        vector<pair<string, unique_ptr<detail::experiment>>> _data;

    public:
        // construction - destruction -----------------------------------
        benchmark() = default;

        benchmark(benchmark const &) = delete;

        // run experiments ----------------------------------------------
        template<class F>
        void run(string const &name, size_t nSample, F &&callable) {
            _data.emplace_back(
                    name, make_unique<detail::experiment_model<TimeT, ClockT>>(
                            nSample, fw(callable)));
        }

        template<class FactorT, class F>
        void run(string const &name, size_t nSample, F &&callable,
                 string const &factorName, initializer_list<FactorT> &&factors) {
            _data.emplace_back(
                    name,
                    make_unique<detail::experiment_model<TimeT, ClockT, FactorT>>(
                            nSample, fw(callable), factorName,
                            forward<initializer_list<FactorT> &&>(factors)));
        }

        template<class F, class It>
        void run(string const &name, size_t nSample, F &&callable,
                 string const &factorName, It beg, It fin) {
            _data.emplace_back(
                    name, make_unique<detail::experiment_model<
                            TimeT, ClockT,
                            typename remove_reference<decltype(*beg)>::type>>(
                            nSample, fw(callable), factorName, beg, fin));
        }

        // utilities ----------------------------------------------------
        void print(const char *benchmarkName, ostream &os) const {
            for (auto &&Pair : _data) {
                os << "{ 'benchmark_name' : '" << benchmarkName << "'";
                os << ", 'experiment_name' : '" << Pair.first << "'";
                os << ", 'time_type' : '" << time_type<TimeT>() << "'";
                Pair.second->print(os);
                os << " } \n";
            }
        }

        void serialize(const char *benchmarkName, const char *filename,
                       std::ios_base::openmode mode = ofstream::out) const {
            ofstream os;
            os.open(filename, mode);
            for (auto &&Pair : _data) {
                os << "{ 'benchmark_name' : '" << benchmarkName << "'";
                os << ", 'experiment_name' : '" << Pair.first << "'";
                os << ", 'time_type' : '" << time_type<TimeT>() << "'";
                Pair.second->print(os);
                os << " } \n";
            }
            os.close();
        }
    };

} // ~ namespace bmk

#undef fw

#endif

