// Unit type ids (PUD numbering) and the names the config file uses for them.
// Ids 0x00-0x39 checked against Data\Rez\unitdata.dat flags/HP (air, sea, undead, caster bits line up per row).
#pragma once
#include <cstdint>

namespace units {

constexpr int kFirstBuilding = 0x3A;  // farm; everything below is a mobile unit (or an unused slot)

struct Entry {
    uint8_t id;
    const char* name;
    bool hero;
};

constexpr Entry kUnits[] = {
    {0x00, "footman", false},          {0x01, "grunt", false},
    {0x02, "peasant", false},          {0x03, "peon", false},
    {0x04, "ballista", false},         {0x05, "catapult", false},
    {0x06, "knight", false},           {0x07, "ogre", false},
    {0x08, "archer", false},           {0x09, "axethrower", false},
    {0x0A, "mage", false},             {0x0B, "death_knight", false},
    {0x0C, "paladin", false},          {0x0D, "ogre_mage", false},
    {0x0E, "dwarves", false},          {0x0F, "goblin_sappers", false},
    {0x10, "attack_peasant", false},   {0x11, "attack_peon", false},
    {0x12, "ranger", false},           {0x13, "berserker", false},
    {0x14, "alleria", true},           {0x15, "teron_gorefiend", true},
    {0x16, "kurdran", true},           {0x17, "dentarg", true},
    {0x18, "khadgar", true},           {0x19, "grom_hellscream", true},
    {0x1A, "human_tanker", false},     {0x1B, "orc_tanker", false},
    {0x1C, "human_transport", false},  {0x1D, "orc_transport", false},
    {0x1E, "elven_destroyer", false},  {0x1F, "troll_destroyer", false},
    {0x20, "battleship", false},       {0x21, "juggernaught", false},
    {0x23, "deathwing", true},         {0x26, "gnomish_submarine", false},
    {0x27, "giant_turtle", false},     {0x28, "flying_machine", false},
    {0x29, "zeppelin", false},         {0x2A, "gryphon_rider", false},
    {0x2B, "dragon", false},           {0x2C, "turalyon", true},
    {0x2D, "eye_of_kilrogg", false},   {0x2E, "danath", true},
    {0x2F, "korgath_bladefist", true}, {0x31, "chogall", true},
    {0x32, "lothar", true},            {0x33, "guldan", true},
    {0x34, "uther_lightbringer", true}, {0x35, "zuljin", true},
    {0x37, "skeleton", false},         {0x38, "daemon", false},
    {0x39, "critter", false},
};

const Entry* FindByName(const char* name);  // case-insensitive, nullptr when unknown
const Entry* FindById(uint8_t id);

}  // namespace units
