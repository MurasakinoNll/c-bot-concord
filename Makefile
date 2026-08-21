# === Project layout =========================================================
SRC_DIR    := srcs
OBJ_DIR    := asms
BIN_DIR    := bins
INC_DIR    := include

CONCORD_DIR := concord

TARGET := $(BIN_DIR)/cfbot

# === Sources / objects =======================================================
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# === Toolchain ================================================================
CC      := cc
CFLAGS  := -g -Wall -Wextra -std=c11 \
           -I$(INC_DIR) -I$(CONCORD_DIR)/include
CPPFLAGS:= -MMD -MP

# Concord itself needs pthread + curl to link.
LDFLAGS := -L$(CONCORD_DIR)/lib
LDLIBS  := -ldiscord -lsqlite3 -lpthread -lcurl -lm

# === Rules ====================================================================
.PHONY: all clean fclean re

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -rf $(BIN_DIR)

release-static: CFLAGS := -O2 -DNDEBUG -static -I$(INC_DIR) -I$(CONCORD_DIR)/include
release-static: LDFLAGS := -L$(CONCORD_DIR)/lib -static
release-static: LDLIBS := -ldiscord -lcurl -lsqlite3 -lssl -lcrypto -lpthread -lz -lm
release-static: $(TARGET)
	strip $(TARGET)

re: fclean all

-include $(DEPS)
