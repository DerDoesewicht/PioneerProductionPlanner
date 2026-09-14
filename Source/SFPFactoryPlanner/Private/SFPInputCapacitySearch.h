#pragma once
namespace SFPInputCapacitySearch {
struct Result { bool Bounded; double Rate; };
// Feasible(0) is checked by the caller to separate unmet fixed wishes.
template<class Predicate> Result Maximize(Predicate Feasible) {
    double Low=0, High=1;
    while (Feasible(High)) {
        Low=High;
        if (High >= 1000000) return {false, Low};
        High = High*2 > 1000000 ? 1000000 : High*2;
    }
    for(int Pass=0; Pass<28 && High-Low>0.00001; ++Pass) {
        const double Mid=(Low+High)*0.5;
        if(Feasible(Mid)) Low=Mid; else High=Mid;
    }
    return {true,Low};
}
}
