NAME = moog
BUNDLE = $(NAME).lv2
INSTALL_DIR = /usr/local/lib/lv2


$(BUNDLE): manifest.ttl $(NAME).ttl $(NAME).so $(NAME)_gui.so
	rm -rf $(BUNDLE)
	mkdir $(BUNDLE)
	cp $^ $(BUNDLE)

$(NAME).so: $(NAME).cpp $(NAME).peg
	g++ -shared -fPIC -DPIC $(NAME).cpp `pkg-config --cflags --libs lv2-plugin` -o $(NAME).so

$(NAME)_gui.so: $(NAME)_gui.cpp $(NAME).peg
	g++ -shared -fPIC -DPIC $(NAME)_gui.cpp `pkg-config --cflags --libs lv2-gui` -o $(NAME)_gui.so
	
$(NAME).peg: $(NAME).ttl
	lv2peg $(NAME).ttl $(NAME).peg

install: $(BUNDLE)
	mkdir -p $(INSTALL_DIR)
	rm -rf $(INSTALL_DIR)/$(BUNDLE)
	cp -R $(BUNDLE) $(INSTALL_DIR)

clean:
	rm -rf $(BUNDLE) $(NAME).so $(NAME)_gui.so $(NAME).peg
