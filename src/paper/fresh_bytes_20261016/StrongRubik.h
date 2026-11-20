#ifndef BIHS_STRONG_RUBIK_H
#define BIHS_STRONG_RUBIK_H
#include "RubiksCube.h"
#include "Bloom.h"
#include <memory>
#include <stdexcept>

// Hash logical pieces and orientations, never padding or object addresses.
template <>
inline uint64_t BiHSBloomFilter<RubiksState>::stable_fingerprint(const RubiksState &s) {
    uint64_t h = 0;
    for (int p = 0; p < 12; ++p) {
        const int piece = s.edge.GetCubeInLoc(p);
        h ^= zobrist_value<40>(p, piece * 2 + s.edge.GetCubeOrientation(piece));
    }
    for (int p = 0; p < 8; ++p) {
        const int piece = s.corner.GetCubeInLoc(p);
        h ^= zobrist_value<40>(p + 12, piece * 3 + s.corner.GetCubeOrientation(piece));
    }
    return h;
}

// Immutable shared tables keep copies made by the searches inexpensive.
class StrongRubik : public RubiksCube {
    struct Data {
        RubiksCube env;
        RubiksState goal;
        RubikPDB edgesA, edgesB, corners;
        RubikArbitraryGoalPDB a, b, c;
        explicit Data(const std::string &directory)
            : edgesA(&env, goal, {1,3,8,9,10,11}, {}),
              edgesB(&env, goal, {0,2,4,5,6,7}, {}),
              corners(&env, goal, {}, {0,1,2,3,4,5,6,7}),
              a(&edgesA), b(&edgesB), c(&corners) {
            if (!edgesA.Load(directory.c_str()) || !edgesB.Load(directory.c_str()) ||
                !corners.Load(directory.c_str()))
                throw std::runtime_error("Missing/incompatible 6+6-edge / 8-corner Rubik PDB files");
            for (auto *p : {&edgesA, &edgesB, &corners}) {
                if (p->PDB.Size() != p->GetPDBSize() || p->HCost(goal, goal) != 0)
                    throw std::runtime_error("Rubik PDB size/goal validation failed");
            }
        }
    };
    std::shared_ptr<Data> data;
public:
    explicit StrongRubik(const std::string &directory) : data(std::make_shared<Data>(directory)) {}
    double HCost(const RubiksState &from, const RubiksState &to) const override {
        return std::max(data->a.HCost(from,to),
                        std::max(data->b.HCost(from,to),data->c.HCost(from,to)));
    }
    double HCost(const RubiksState &from, const RubiksState &to, double) const override {
        return HCost(from,to);
    }
    void StoreGoal(RubiksState&) {} // HCost always honors its explicit target.
    uint64_t GetStateHash(const RubiksState &s) const override {
        return BiHSBloomFilter<RubiksState>::stable_fingerprint(s);
    }
    uint64_t PDBBytes() const {
        return (data->edgesA.GetPDBSize()+1)/2 + (data->edgesB.GetPDBSize()+1)/2 +
               (data->corners.GetPDBSize()+1)/2;
    }
};
#endif
