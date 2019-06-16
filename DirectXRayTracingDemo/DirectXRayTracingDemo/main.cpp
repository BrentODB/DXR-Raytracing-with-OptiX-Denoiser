#include "stdafx.h"
#include "Demo.h"

const char g_szClassName[] = "myWindowClass";
const char windowName[] = "DirectXRayTracingDemo";

std::chrono::steady_clock::time_point start;

Demo* pDemo = nullptr;
void DestroyDemo();
bool showInfo = true;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_KEYDOWN:
	{
		switch (wParam)
		{
		case VK_UP:
			pDemo->mSampleCapAmount++;
			break;
		case VK_DOWN:
			if (pDemo->mSampleCapAmount > 1)
			{
				pDemo->mSampleCapAmount--;
				pDemo->mpCamera->m_Moved = true;
			}
			break;
		case 'L':
			pDemo->mSampleCap = !pDemo->mSampleCap;
			break;
		case 'P': 
			showInfo = !showInfo;
			break;
		case 'M':
			pDemo->mMoveLight = !pDemo->mMoveLight;
			break;
		}
		break;
	}	
	case WM_RBUTTONDOWN:
	{
		pDemo->mDenoiseOutput = !pDemo->mDenoiseOutput;
		break;
	}
	case WM_LBUTTONDOWN:
		pDemo->mpCamera->mMouseDown = true;
		if (pDemo)
		{
			pDemo->mpCamera->StartMouseCapture();
		}
		break;
	case WM_LBUTTONUP:
		pDemo->mpCamera->mMouseDown = false;
		break;
	case WM_MBUTTONUP:
		pDemo->mSampleBool = !pDemo->mSampleBool;
		break;
	case WM_MOUSEWHEEL:
		pDemo->mpCamera->m_MoveSpeed += GET_WHEEL_DELTA_WPARAM(wParam) *0.5f;
		if (pDemo->mpCamera->m_MoveSpeed < 0.0f)
		{
			pDemo->mpCamera->m_MoveSpeed = 0.1f;
		}
		break;
	case WM_PAINT:
		if (pDemo)
		{
			pDemo->Update();

			int fps = 0;
			static int frameCnt = 0;
			static double elapsedTime = 0.0f;
			auto currentTime = std::chrono::high_resolution_clock::now();
			double totalTime = std::chrono::duration<float, std::milli>(currentTime - start).count() / 1000.0f;
			frameCnt++;

			// Compute averages over one second period.
			if ((totalTime - elapsedTime) >= 0.1f)
			{
				float capStartTime = 0;
				float diff = static_cast<float>(totalTime - elapsedTime);
				fps = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.
				frameCnt = 0;
				elapsedTime = totalTime;		
				
				std::wstringstream windowText;
				if (showInfo)
				{
					windowText << "DirectXRayTracingDemo FPS: " << fps << " | Resolution: " << pDemo->mWidth << "/" << pDemo->mHeight << " | Samples: " << pDemo->mSampleCount-1/*Sample 0*/;
					if (pDemo->mSampleCap)
					{
						windowText << " | Samples Cap: " << pDemo->mSampleCapAmount;
					}
					if (pDemo->mDenoiseOutput)
					{
						windowText << " | Denoised with OptiX 6.0";
					}
				}
				else
				{
					windowText << "DirectXRayTracingDemo Resolution: " << pDemo->mWidth << "/" << pDemo->mHeight;
					if (pDemo->mDenoiseOutput)
					{
						windowText << " | Denoised with OptiX 6.0";
					}
				}
				SetWindowTextW(hwnd, windowText.str().c_str());
			}
			
		}
		break;
	case WM_SIZE:
		RECT rect;
		if (GetWindowRect(hwnd, &rect))
		{
			int widthTemp = rect.right - rect.left;
			int heightTemp = rect.bottom - rect.top;
			pDemo->onResize(heightTemp, widthTemp);
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		DestroyDemo();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

void DestroyDemo()
{
	if (pDemo != nullptr)
	{
		delete pDemo;
		pDemo = nullptr;
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASSEX wc;
	HWND hwnd;
	MSG Msg;

	start = std::chrono::high_resolution_clock::now();

	//Step 1: Registering the Window Class
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = g_szClassName;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, "Window Registration Failed!", "Error!",
			MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	// Step 2: Creating the Window
	hwnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		g_szClassName,
		windowName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, WIDTH, HEIGHT,
		NULL, NULL, hInstance, pDemo);

	if (hwnd == NULL)
	{
		MessageBox(NULL, "Window Creation Failed!", "Error!",
			MB_ICONEXCLAMATION | MB_OK);
		return 0;
	}

	ShowWindow(hwnd, nCmdShow);
	UpdateWindow(hwnd);

	//DEMO Start
	pDemo = new Demo(hwnd);

	// Step 3: The Message Loop
	while (GetMessage(&Msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
	return Msg.wParam;
}