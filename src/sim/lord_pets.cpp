#include "sim/lord_pets.h"

#include <cmath>

namespace mu::sim {

uint32_t petExpAt(int level) {
    if (level <= 1) return 0;
    const uint64_t l = uint64_t(level);
    return uint32_t((10 + l) * l * l * l * 100);
}

uint32_t petExp(const Held& pet) {
    return uint32_t(pet.powers[0]) | uint32_t(pet.powers[1]) << 8 | uint32_t(pet.powers[2]) << 16 |
           uint32_t(pet.wing) << 24;
}

void setPetExp(Held& pet, uint32_t exp) {
    for (int i = 0; i < 3; ++i) pet.powers[i] = uint8_t(exp >> (8 * i));
    pet.wing = uint8_t(exp >> 24);
}

int petLevel(const Held& pet) {
    const uint32_t exp = petExp(pet);
    int level = 1;
    while (level < kPetTopLevel && exp >= petExpAt(level + 1)) ++level;
    return level;
}

bool darkRaven(const content::ItemRow& row) {
    return row.group == kGroupPets && row.number == kDarkRavenNumber;
}

bool levelledPet(const content::ItemRow& row) { return darkHorse(row) || darkRaven(row); }

Needs asks(const content::ItemRow& row, const Held& what) {
    Needs n = asks(row, what.refinement, what.excellent != 0);
    if (darkHorse(row)) n.level = row.needLevel + 2 * petLevel(what);
    return n;
}

PetPower petPower(const content::ItemRow& row, const Held& what) {
    PetPower power = petPower(row);
    if (darkHorse(row)) power.taken = double(100 - (30 + petLevel(what)) / 2) / 100.0;
    return power;
}

int horseDefense(int agility, int level) { return 5 + agility / 20 + 2 * level; }

int bleedPet(Held& pet) {
    const int was = petLevel(pet);
    const uint32_t span = petExpAt(was + 1) - petExpAt(was);
    const uint32_t exp = petExp(pet);
    setPetExp(pet, exp - std::min(exp, span / 10));
    const int now = petLevel(pet);
    return now < was ? now : 0;
}

int64_t petValue(const content::ItemRow& row, const Held& what) {
    const int64_t each = darkHorse(row) ? 2000000 : darkRaven(row) ? 1000000 : 0;
    return each * petLevel(what);
}

int64_t mendPrice(int64_t value, int life) {
    if (value <= 0 || life >= 255) return 0;
    const double root = std::sqrt(double(std::min<int64_t>(value, 400000000)));
    double price = 3.0 * root * std::sqrt(root) * (1.0 - double(std::max(0, life)) / 255.0) + 1.0;
    if (life <= 0) price *= 2.0;
    return int64_t(price);
}

}  // namespace mu::sim
