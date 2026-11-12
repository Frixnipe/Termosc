#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>
#include <string>

// defining keypresses
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define ENTER 13
#define ESC 27

using namespace std;

float amp = 1; // the amplification of the oscilloscope

/*
TODO:
- gradients
- (every color in gradient)
- probably move the config over to a file because reconfiging every time is getting annoying
- a buncha code optimizations that I am usually too lazy to do
- config UI overhaul to comply with other CLI UIs
- make oscilloscope() run on a forever loop in main() instead so that I can also use it from config() as a 'preview'
- a 2D oscilloscope maybeeee????????
*/

// set a handle to the console window for colors and shit
HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);

// used for getting console size
CONSOLE_SCREEN_BUFFER_INFO csbi;
void getWindowSize(int& w, int& h) {
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

int width = 0, height = 0;

vector <CHAR_INFO> t;

unsigned short bg_color = 0, osc_color = 0xf, style = 1, speed = 16; // config variables

void updateWindowSize(int w, int h) {
	if (w != width || h != height) {
		system("cls");
		width = w;
		height = h;
		t.assign(w * h, { 0 });

		// update the console's internal buffer so that it always writes only to the visible area
		SMALL_RECT windowSize = { 0, 0, w - 1, h - 1 };
		SetConsoleScreenBufferSize(hc, { (SHORT)w, (SHORT)h });
		SetConsoleWindowInfo(hc, TRUE, &windowSize);
	}
}

void setPixel(int x, int y, int col) {
	if (x < 0 || x >= width || y < 0 || y >= height * 2) return; // don't write outside of the visible area
	t[y / 2 * width + x].Char.UnicodeChar = L'▀';
	if (y % 2) {
		// bottom half; set background color
		t[y / 2 * width + x].Attributes = (t[y / 2 * width + x].Attributes & 0x0F) | ((col << 4) & 0xF0);
	}
	else {
		// top half; set text color
		t[y / 2 * width + x].Attributes = (t[y / 2 * width + x].Attributes & 0xF0) | (col & 0x0F);
	}
}

void setText(int x, int y, int col, wstring txt) {
	for (int i = 0; i < txt.length(); i++) {
		if (x + i >= width) return;
		t[y * width + x + i].Char.UnicodeChar = txt[i];
		t[y * width + x + i].Attributes = col;
	}
}

void oscilloscope() {
	HRESULT hr;
	CoInitialize(NULL);

	// initialize WASAPI variables
	IMMDeviceEnumerator* pEnumerator = NULL;
	IMMDevice* pDevice = NULL;
	IAudioClient* pAudioClient = NULL;
	IAudioCaptureClient* pCaptureClient = NULL;
	WAVEFORMATEX* pwfx = NULL;

	// create instance
	CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
	pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);

	// configure instance
	pAudioClient->GetMixFormat(&pwfx); // set mix format
	pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, pwfx, NULL); // loopback mode so that we receive audio
	pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
	pAudioClient->Start();

	static int w, h; // actual width and height because yes
	while (1) {
		// update window size
		getWindowSize(w, h);
		updateWindowSize(w, h);

		// clear CHAR_INFO
		// fill(t.begin(), t.end(), CHAR_INFO{ L' ', static_cast<unsigned short>((int)bg_color << 4)});
		// I have no idea how the hell I could convert that int into unsigned short without the compiler complaining
		for (int i = 0; i < h * 2; i++) {
			for (int ii = 0; ii < w; ii++) setPixel(ii, i, bg_color);
		}

		/* CLANKER CODE START (I hate myself) */

		// capture audio
		UINT32 packetLength = 0;
		pCaptureClient->GetNextPacketSize(&packetLength);

		if (packetLength > 0) { // if there is data
			BYTE* pData;
			UINT32 fn; // number of frames available
			DWORD flags;
			pCaptureClient->GetBuffer(&pData, &fn, &flags, NULL, NULL);
			float* fData = (float*)pData;

			// actually rendering it, here we go
			for (int x = 0; x < w; x++) {
				// Map the current X to a sample in the audio buffer
				int sampleIdx = (x * fn / w) * pwfx->nChannels; // nChannels refers to number of channels (mono, stereo, etc.)
				if (sampleIdx >= fn * pwfx->nChannels) break;

				float sample = fData[sampleIdx]; // is always between -1.0f and 1.0f

				/* CLANKER CODE END */

				// clamp sample to window height
				int y = static_cast<int>(((sample + 1.0f) / 2.0f) * (h * 2 * amp - 1));

				/*
				this is where the different "styles" are defined
				*/

				switch (style) {
				case 0:
					// dotted style
					setPixel(x, y - h * (amp - 1), osc_color);
					break;
				case 1:
					// line style
					static int yl;
					if (!x) yl = y; // y position of the last column
					for (int v = min(y, yl); v <= max(y, yl); v++) {
						// setPixel(x, v * 3 - h * 2, osc_color);
						setPixel(x, v - h * (amp - 1), osc_color);
					}
					yl = y;
					break;
				case 2:
					// filled style
					for (int v = min(y, h * amp - 1); v <= max(y, h * amp - 1); v++) {
						setPixel(x, v - h * (amp - 1), osc_color);
					}
				}
			}
			pCaptureClient->ReleaseBuffer(fn);
		}

		// actually display the stuff
		COORD bufferSize = { w, h };
		COORD bufferCoord = { 0, 0 };
		SMALL_RECT writeRegion = { 0, 0, w - 1, h - 1 };
		WriteConsoleOutputW(hc, t.data(), bufferSize, bufferCoord, &writeRegion);

		Sleep(speed); // change refresh rate here; needs to be at least 1 so that it doesn't flicker as much

		if (_kbhit()) break; // break and open config if any key is pressed
	}
	// all the code required for breaking in case I do that
	pAudioClient->Stop();
	CoTaskMemFree(pwfx);
	pCaptureClient->Release();
	pAudioClient->Release();
	pDevice->Release();
	pEnumerator->Release();
	CoUninitialize();
}

void config() {
	static int sel = 0; // selected category
	static int w, h; // window width & height
	while (1) {
		// clear the vector array
		fill(t.begin(), t.end(), CHAR_INFO{ L' ', 0 });

		// update window size
		getWindowSize(w, h);
		updateWindowSize(w, h);

		// get inputs n' stuff
		if (_kbhit()) {
			switch (_getch()) {
			case KEY_UP:
				if (sel > 0) sel--;
				break;
			case KEY_DOWN:
				if (sel < 4) sel++;
				break;
			case KEY_LEFT:
				switch (sel) {
				case 0:
					if (osc_color > 0) osc_color--;
					break;
				case 1:
					if (bg_color > 0) bg_color--;
					break;
				case 2:
					if (style > 0) style--;
					break;
				case 3:
					if (speed == 1) speed = 8;
					else if (speed == 8) speed = 16;
					else if (speed == 16) speed = 33;
					break;
				case 4:
					if (amp > 10) amp -= 1;
					else if (amp > 0.1) amp -= 0.1;
					break;
				}
				break;
			case KEY_RIGHT:
				switch (sel) {
				case 0:
					if (osc_color < 15) osc_color++;
					break;
				case 1:
					if (bg_color < 15) bg_color++;
					break;
				case 2:
					if (style < 2) style++;
					break;
				case 3:
					if (speed == 33) speed = 16;
					else if (speed == 16) speed = 8;
					else if (speed == 8) speed = 1;
					break;
				case 4:
					if (amp < 10) amp += 0.1;
					else if (amp < 99) amp += 1;
					break;
				}
				break;
			case ENTER:
				return;
				break;
			case ESC:
				return;
				break;
			}
		}

		// ← ↑ → ↓
		if (w < 55 || h < 10) {
			// system("cls");
			setText(0, 0, 0xf, L"The window is too small to display the config!");
		}
		else {
			// title
			for (int i = 0; i < w; i++) {
				t[i].Char.UnicodeChar = L' ';
				t[i].Attributes = 0xf0;
			}
			setText((w - 6) / 2, 0, 0xf0, L"CONFIG");

			// oscilloscope & background colors
			setText(0, 3, (!sel) ? 0xf : 8, L"oscilloscope color: ");
			setText(0, 5, (sel == 1) ? 0xf : 8, L"background color:");
			for (unsigned short i = 0; i < 16; i++) {
				if (osc_color == i) {
					if (osc_color) setText(20 + i * 2, 3, i, L"█ ");
					else setText(20 + i * 2, 3, 0xf, L"▒ ");
				}
				else setText(20 + i * 2, 3, i, L"░ ");
				if (bg_color == i) {
					if (bg_color) setText(20 + i * 2, 5, i, L"█ ");
					else setText(20 + i * 2, 5, 0xf, L"▒ ");
				}
				else setText(20 + i * 2, 5, i, L"░ ");
			}

			// style
			setText(0, 7, (sel == 2) ? 0xf : 8, L"style:");
			setText(19, 7, (!style) ? 15 : 8, L"dotted");
			setText(27, 7, (style == 1) ? 15 : 8, L"line");
			setText(33, 7, (style == 2) ? 15 : 8, L"filled");

			// speed
			setText(0, 9, (sel == 3) ? 0xf : 8, L"speed:");
			setText(19, 9, (speed == 33) ? 0xf : 8, L"30 FPS");
			setText(27, 9, (speed == 16) ? 0xf : 8, L"60 FPS");
			setText(35, 9, (speed == 8) ? 15 : 8, L"120 FPS");
			setText(44, 9, (speed == 1) ? 15 : 8, L"uncapped");

			// amplitude
			setText(0, 11, (sel == 4) ? 0xf : 8, L"amplitude:");
			setText(19, 11, 0xf, (floor(amp / 10)) ? to_wstring(amp).substr(0, 2) : to_wstring(amp).substr(0, 3));

			// key instructions
			setText(0, h - 1, 0xf, L"↑ ↓");
			setText(3, h - 1, 8, L" - choose setting, ");
			setText(22, h - 1, 0xf, L"← →");
			setText(25, h - 1, 8, L" - change setting, ");
			setText(44, h - 1, 0xf, L"↲");
			setText(45, h - 1, 8, L" - back");

		}
		// ...and after all that madness, write it all out
		COORD bufferSize = { w, h };
		COORD bufferCoord = { 0, 0 };
		SMALL_RECT writeRegion = { 0, 0, w - 1, h - 1 };
		WriteConsoleOutputW(hc, t.data(), bufferSize, bufferCoord, &writeRegion);

		Sleep(16);
	}
}

int main() {
	_setmode(_fileno(stdout), _O_U16TEXT); // allow Unicode in console

	// hide cursor
	CONSOLE_CURSOR_INFO cursorInfo;
	GetConsoleCursorInfo(hc, &cursorInfo);
	cursorInfo.bVisible = false;
	SetConsoleCursorInfo(hc, &cursorInfo);

	while (1) {
		oscilloscope();
		config();
	}

	return 0;
}