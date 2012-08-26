HAMA MCE event client for XBMC
=========================
based on the event client from AaYJFG

The event client resolves the following issues:

* Keyrepeat and mouse works now as expected. 
* The remote registers 2 input devices, some keys register as mouse buttons, other as keyboard keys. This makes mapping in keymap.xml difficult. The attached event driver sends key and action events which are easy to map.
* Right mouse button and info key have the same keycode, but it is possible to distinguish them by counting the different number of key-up events. The attached code does this, so that we can use one key as "info" and the other as "right mouse button" (or "contextmenu").
* A matching keymap is provided (contains only a single entry, since the event client already sends "intuitive" events to xbmc).
* The event client is started by udev under linux, and stopped when the device is disconnected.


How to compile
----------------------------------------

	$ apt-get install libusb-1.0-0-dev
	$ make
	$ make install
	
	
Uninstall
----------------------------------------
	
	$ make uninstall


Credits to AaYJFG