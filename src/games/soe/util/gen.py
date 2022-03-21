#!/usr/bin/python3


"""Use this script to generate location and item data from evermizer source code."""


ALCHEMY_BASE_NUMBER = 0
BOSSES_BASE_NUMBER = 50
GOURDS_BASE_NUMBER = 100
NPC_BASE_NUMBER = 400  # not implemented
SNIFF_BASE_NUMBER = 500  # not implemented
TRAP_BASE_NUMBER = 900

alchemy = [  # there is no json for this (yet) :/
    # [itemid, name, [addr,mask]
    [0x1000, "Acid Rain",        [0x7e2570,0x01]],
    [0x1001, "Atlas",            [0x7e2570,0x02]],
    [0x1002, "Barrier",          [0x7e2570,0x04]],
    [0x1003, "Call Up",          [0x7e2570,0x08]],
    [0x1004, "Corrosion",        [0x7e2570,0x10]],
    [0x1005, "Crush",            [0x7e2570,0x20]],
    [0x1006, "Cure",             [0x7e2570,0x40]],
    [0x1007, "Defend",           [0x7e2570,0x80]],
    [0x1008, "Double Drain",     [0x7e2571,0x01]],
    [0x1009, "Drain",            [0x7e2571,0x02]],
    [0x100a, "Energize",         [0x7e2571,0x04]],
    [0x100b, "Escape",           [0x7e2571,0x08]],
    [0x100c, "Explosion",        [0x7e2571,0x10]],
    [0x100d, "Fireball",         [0x7e2571,0x20]],
    [0x100e, "Fire Power",       [0x7e2571,0x40]],
    [0x100f, "Flash",            [0x7e2571,0x80]],
    [0x1010, "Force Field",      [0x7e2572,0x01]],
    [0x1011, "Hard Ball",        [0x7e2572,0x02]],
    [0x1012, "Heal",             [0x7e2572,0x04]],
    [0x1013, "Lance",            [0x7e2572,0x08]],
    #[0x1014, "Laser",            [0x7e2572,0x10]],
    [0x1015, "Levitate",         [0x7e2572,0x20]],
    [0x1016, "Lightning Storm",  [0x7e2572,0x40]],
    [0x1017, "Miracle Cure",     [0x7e2572,0x80]],
    [0x1018, "Nitro",            [0x7e2573,0x01]],
    [0x1019, "One Up",           [0x7e2573,0x02]],
    [0x101a, "Reflect",          [0x7e2573,0x04]],
    [0x101b, "Regrowth",         [0x7e2573,0x08]],
    [0x101c, "Revealer",         [0x7e2573,0x10]],
    [0x101d, "Revive",           [0x7e2573,0x20]],
    [0x101e, "Slow Burn",        [0x7e2573,0x40]],
    [0x101f, "Speed",            [0x7e2573,0x80]],
    [0x1020, "Sting",            [0x7e2574,0x01]],
    [0x1021, "Stop",             [0x7e2574,0x02]],
    [0x1022, "Super Heal",       [0x7e2574,0x04]],
]

boss_drops = [  # taken from data.h, evermizer wiki and ram map. sadly this can not be extracted from bosses.json (yet)
    [0x000, "Nothing"],
    [0x127, "Wheel"],
    [0x112, "Gladiator Sword"],
    [0x113, "Crusader Sword"],
    [0x115, "Spider Claw"],
    [0x116, "Bronze Axe"],
    [0x119, "Horn Spear"],
    [0x11a, "Bronze Spear"],
    [0x11b, "Lance (Weapon)"],
    [0x802, "Honey"],
    [0x1f4, "Progressive breast"], # vanilla: dino skin / talons
    #[0x11e, "Bazooka+Shells / Shining Armor / 5k Gold"],
    [0x11d, "Bazooka+Shells / Shining Armor / 5k Gold"],
    [0x129, "10,000 Gold Coins"],
    [0x206, "Mud Pepper"],
    [0x126, "Diamond Eye"],
]

traps = [  # taken from data.h. TODO: extract
    [0x151, "Quake Trap"],
    [0x152, "Poison Trap"],
    [0x153, "Confound Trap"],
    [0x154, "HUD Trap"],
    [0x155, "OHKO Trap"],
]


def main(src_dir, dst_dir):
    import os.path
    import json
    import codecs
    import csv
    from sys import version_info
    with codecs.open(os.path.join(dst_dir, 'locations.inc'), 'wb', encoding='utf-8') as floc:
        with codecs.open(os.path.join(dst_dir, 'items.inc'), 'wb', encoding='utf-8') as fitems:
            floc.write('    // alchemy\n')
            fitems.write('    // alchemy\n')
            for n, spell in enumerate(alchemy, ALCHEMY_BASE_NUMBER):
                s = '    {{0x%06x, 0x%02x},  %3d}, // %s\n' % (spell[2][0], spell[2][1], n, spell[1])
                floc.write(s)
                s = '    {%3d, {%3d, 0x%03x}}, // %s\n' % (n, 1, spell[0], spell[1])
                fitems.write(s)

            with open(os.path.join(src_dir, 'bosses.json'), 'r') as fin:
                floc.write('    // bosses\n')
                fitems.write('    // bosses\n')
                n = BOSSES_BASE_NUMBER
                for loc in json.load(fin):
                    s = '    {{%s, %s},  %3d}, // %s\n' % (loc['bit'][0], loc['bit'][1], n, loc['name'])
                    if not loc['drop']: s = '//' + s[2:]
                    floc.write(s)
                    n += 1
                for n, drop in enumerate(boss_drops, BOSSES_BASE_NUMBER):
                    s = '    {%3d, {%3d, 0x%03x}}, // %s\n' % (n, 1, drop[0], drop[1])
                    fitems.write(s)

            with codecs.open(os.path.join(src_dir, 'gourds.csv'), 'r', encoding='utf-8') as fin:
                if version_info[0]==2: # sadly py2 CSV is bytes, py3 CSV is unicode
                    src = fin.read().encode('ascii', 'replace').replace('\r\n', '\n')
                else:
                    src = fin.read().replace('\r\n', '\n')
                data = csv.reader(src.split('\n'))
                next(data) # skip header
                checknr=0
                floc.write('    // gourds\n')
                fitems.write('    // gourds\n')
                for row in data:
                    # NOTE: since the csv has more data than required, we try to validate stuff during parsing
                    if r'unreachable' in row[17].lower():
                        continue
                    csv_checknr = int(row[0])
                    assert(csv_checknr == checknr)
                    addr,mask = row[5].split('&')
                    addr = '0x7e' + addr[1:]
                    s = '    {{%s, %s}, %3d}, // #%d, %s\n' % (addr, mask, GOURDS_BASE_NUMBER + checknr, checknr, row[1])
                    floc.write(s)
                    prize = int(row[6], 0)
                    try:
                        # actual amount, only used for moniez 
                        amount = int(row[8], 0)
                    except:
                        try:
                            # for ingredients the game will have to convert that back to "next add"
                            # the game should save and restore what was in the "next add" field
                            amount = int(row[9], 0) + 1
                        except:
                            # default to 1 piece if no amount was specified
                            amount = 1
                    name = row[7]
                    if amount > 1:  # append amount if not already part of name
                        try:
                            int(name.split(' ')[0])
                        except:
                            name = '%d %s' % (amount, name)
                    s = '    {%3d, {%3d, 0x%03x}}, // #%d, %s\n' % (GOURDS_BASE_NUMBER + checknr, amount, prize, checknr, name)
                    fitems.write(s)
                    checknr += 1

            fitems.write('    // traps\n')
            for n, drop in enumerate(traps, TRAP_BASE_NUMBER):
                s = '    {%3d, {%3d, 0x%03x}}, // %s\n' % (n, 1, drop[0], drop[1])
                fitems.write(s)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Generate Evermizer AP Client data')
    parser.add_argument('src', type=str, nargs='?', default='../../../../../evermizer')
    parser.add_argument('dst', type=str, nargs='?', default='..')
    args = parser.parse_args()
    main(args.src, args.dst)
