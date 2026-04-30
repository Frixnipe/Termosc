#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <stdio.h>
#include <windows.h>
#include <conio.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <vector>

// defining keypresses
#define KEY_UP 72
#define KEY_DOWN 80
#define KEY_LEFT 75
#define KEY_RIGHT 77
#define ENTER 13
#define ESC 27

using namespace std;

/*
TODO:
- color / glow effects n' stuff
- a 2D oscilloscope maybeeee????????

CONFIGS
- fade amount
- color of each fade
- reactiveness
*/

// set a handle to the console window for colors and shit
HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);

// used for getting console size
CONSOLE_SCREEN_BUFFER_INFO csbi;
void getWindowSize(int& rows, int& columns) {
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
	columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
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
	if (x < 0 || x >= width || y < 0 || y > height * 2) return; // don't write outside of the visible area
	t[y / 2 * width + x].Char.UnicodeChar = L'▀';
	if (y % 2) {
		// bottom half; set background color
		t[y / 2 * width + x].Attributes = (t[y / 2 * width + x].Attributes & 0xFF0F) | ((col << 4) & 0xF0);
	}
	else {
		// top half; set text color
		t[y / 2 * width + x].Attributes = (t[y / 2 * width + x].Attributes & 0xFFF0) | (col & 0x0F);
	}
}

void setText(int x, int y, int col, wstring txt) {
	for (int i = 0; i < txt.length(); i++) {
		if (x + i >= width) return;
		t[y * width + x + i].Char.UnicodeChar = txt[i];
		t[y * width + x + i].Attributes = col;
	}
}

// output a number of spaces (useful for centering stuff)
void spaces(int x) {
	for (int i = 0; i < x; i++) wprintf(L" ");
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

	int w, h; // actual width and height because yes
	while (1) {
		// update window size
		getWindowSize(h, w); // curse my school project I copied this function from, and the fact that I had to debug for 3 hours to find out that the parameters inside this are swapped
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
				int y = static_cast<int>(((sample + 1.0f) / 2.0f) * (h * 2 - 1));

				/*
				this is where the different "styles" are defined
				*/

				switch (style) {
				case 0:
					// dotted style
					setPixel(x, y, osc_color);
					break;
				case 1:
					// line style
					static int yl;
					if (!x) yl = y; // y position of the last column
					for (int v = min(y, yl); v <= max(y, yl); v++) {
						setPixel(x, v, osc_color);
					}
					yl = y;
					break;
				case 2:
					// filled style
					for (int v = min(y, h); v <= max(y, h); v++) {
						setPixel(x, v, osc_color);
					}
					break;
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
	int sel = 0; // selected category
	while (1) {
		// system("pause");
		int w, h;

		// my god... i need to implement refreshing this page with the vector array too
		// *sigh*
		// clear the vector array
		fill(t.begin(), t.end(), CHAR_INFO{ L' ', 0 });

		// get the window size
		getWindowSize(h, w);
		updateWindowSize(w, h);

		// get inputs n' stuff
		if (_kbhit()) {
			switch (_getch()) {
			case KEY_UP:
				if (sel > 0) sel--;
				break;
			case KEY_DOWN:
				if (sel < 3) sel++;
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
			// wprintf(L"The window is too small to display the config!");
		}
		else {
			// title
			for (int i = 0; i < w; i++) {
				t[i].Char.UnicodeChar = L' ';
				t[i].Attributes = 0xf0;
			}
			setText((w - 6) / 2, 0, 0xf0, L"CONFIG");

			// oscilloscope color
			if (!sel) {
				setText(0, 3, 0xf, L"oscilloscope color: ");
			}
			else {
				setText(0, 3, 8, L"oscilloscope color: ");
			}
			for (unsigned short i = 0; i < 16; i++) {
				if (osc_color == i) {
					if (osc_color) setText(20 + i * 2, 3, i, L"█ ");
					else {
						setText(20 + i * 2, 3, 15, L"▒ ");
					}
				}
				else setText(20 + i * 2, 3, i, L"░ ");
			}

			// background color
			if (sel == 1) {
				setText(0, 5, 0xf, L"background color:   ");
			}
			else {
				setText(0, 5, 8, L"background color:   ");
			}
			for (unsigned short i = 0; i < 16; i++) {
				if (bg_color == i) {
					if (bg_color) setText(20 + i * 2, 5, i, L"█ ");
					else {
						setText(20 + i * 2, 5, 15, L"▒ ");
					}
				}
				else setText(20 + i * 2, 5, i, L"░ ");
			}

			// style
			if (sel == 2) setText(0, 7, 0xf, L"style:              ");
			else setText(0, 7, 8, L"style:              ");
			if (!style) setText(19, 7, 15, L"dotted  ");
			else setText(19, 7, 8, L"dotted  ");
			if (style == 1) setText(27, 7, 15, L"line  ");
			else setText(27, 7, 8, L"line  ");
			if (style == 2) setText(33, 7, 15, L"filled  ");
			else setText(33, 7, 8, L"filled  ");

			// speed
			if (sel == 3) setText(0, 9, 0xf, L"speed:              ");
			else setText(0, 9, 8, L"speed:              ");
			if (speed == 33) setText(19, 9, 15, L"30 FPS  ");
			else setText(19, 9, 8, L"30 FPS  ");
			if (speed == 16) setText(27, 9, 15, L"60 FPS  ");
			else setText(27, 9, 8, L"60 FPS  ");
			if (speed == 8) setText(35, 9, 15, L"120 FPS  ");
			else setText(35, 9, 8, L"120 FPS  ");
			if (speed == 1) setText(44, 9, 15, L"uncapped  ");
			else setText(44, 9, 8, L"uncapped  ");

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

		Sleep(33);
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

	// output key values because yes
	/*
	while (1) {
		if (_kbhit()) cout << _getch() << "\n";
	}
	*/

	return 0;
}