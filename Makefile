GAMES := soe som

GAME_ALL := $(addsuffix -all, $(GAMES))
GAME_TARGETS := $(addsuffix -native, $(GAMES))
GAME_TARGETS := $(GAME_TARGETS) $(addsuffix -wasm, $(GAMES))

ifneq ($(OS),Windows_NT)
GAME_TARGETS := $(GAME_TARGETS) $(addsuffix -cross, $(GAMES))
endif

$(info games:     $(GAMES))
$(info games all: $(GAME_ALL))
$(info games tgt: $(GAME_TARGETS))

all: $(GAME_ALL)

$(GAME_ALL) $(GAME_TARGETS): # <game>-<target>: => make -f <game>.mk <target>
	$(MAKE) -f "$(word 1,$(subst -, ,$@)).mk" "$(word 2,$(subst -, ,$@))"

native:
	$(foreach game,$(GAMES),$(MAKE) -f "$(game).mk" native ;)

wasm:
	$(foreach game,$(GAMES),$(MAKE) -f "$(game).mk" wasm ;)

ifneq ($(OS),Windows_NT)
cross:
	$(foreach game,$(GAMES),$(MAKE) -f "$(game).mk" cross ;)
endif

clean:
	$(foreach game,$(GAMES),$(MAKE) -f "$(game).mk" clean ;)

.PHONY: all clean $(GAME_ALL) $(GAME_TARGETS)
