#include "FaceTrackingRenderer2D.h"
#include "FaceTrackingUtilities.h"
#include "pxccapture.h"

#define HEAD_THRESHOLD 200
#define MOUSE_INC_THRESHOLD 10
#define NUM_OF_AVGS 30
static int avgIdx;

static int xAvg[NUM_OF_AVGS] = { 0 };
static int yAvg[NUM_OF_AVGS] = { 0 };
static int yawAvg[NUM_OF_AVGS] = { 0 };
static int pitchAvg[NUM_OF_AVGS] = { 0 };
static int rollAvg[NUM_OF_AVGS] = { 0 };
static bool eyeMode = true;			// true -> in eye mode / false -> in head mode
volatile static bool mouseIsDown;
volatile static int mouseDownCounter = 0;

static int dwellTime = 0;
static int x_previous = 0;
static int y_previous = 0;

static int headPointX;
static int headPointY;

static int screen_height = 1440;
static int screen_width = 2180;

extern volatile bool need_calibration;

int globalLeftEyeIntensity;
int globalTongueOutIntensity;

int tongueOutCounter = 0;

FaceTrackingRenderer2D::~FaceTrackingRenderer2D()
{
}

FaceTrackingRenderer2D::FaceTrackingRenderer2D(HWND window) : FaceTrackingRenderer(window)
{
}





void FaceTrackingRenderer2D::DrawGraphics(PXCFaceData* faceOutput)
{
	assert(faceOutput != NULL);
	if (!m_bitmap) return;

	const int numFaces = faceOutput->QueryNumberOfDetectedFaces();
	for (int i = 0; i < numFaces; ++i)
	{
		PXCFaceData::Face* trackedFace = faceOutput->QueryFaceByIndex(i);
		assert(trackedFace != NULL);
		if (FaceTrackingUtilities::IsModuleSelected(m_window, IDC_LOCATION) && trackedFace->QueryDetection() != NULL)
			DrawLocation(trackedFace);
		if (FaceTrackingUtilities::IsModuleSelected(m_window, IDC_LANDMARK) && trackedFace->QueryLandmarks() != NULL)
			DrawLandmark(trackedFace);
		if (FaceTrackingUtilities::IsModuleSelected(m_window, IDC_POSE))
			DrawPoseAndPulse(trackedFace, i);
		//if (FaceTrackingUtilities::IsModuleSelected(m_window, IDC_EXPRESSIONS) && trackedFace->QueryExpressions() != NULL)
		DrawExpressions(trackedFace, i);
	}
}

void FaceTrackingRenderer2D::DrawBitmap(PXCCapture::Sample* sample)
{
	if (m_bitmap)
	{
		DeleteObject(m_bitmap);
		m_bitmap = 0;
	}

	PXCImage* image = sample->color;

	PXCImage::ImageInfo info = image->QueryInfo();
	PXCImage::ImageData data;
	if (image->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &data) >= PXC_STATUS_NO_ERROR)
	{
		HWND hwndPanel = GetDlgItem(m_window, IDC_PANEL);
		HDC dc = GetDC(hwndPanel);
		BITMAPINFO binfo;
		memset(&binfo, 0, sizeof(binfo));
		binfo.bmiHeader.biWidth = data.pitches[0] / 4;
		binfo.bmiHeader.biHeight = -(int)info.height;
		binfo.bmiHeader.biBitCount = 32;
		binfo.bmiHeader.biPlanes = 1;
		binfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		binfo.bmiHeader.biCompression = BI_RGB;
		Sleep(1);
		m_bitmap = CreateDIBitmap(dc, &binfo.bmiHeader, CBM_INIT, data.planes[0], &binfo, DIB_RGB_COLORS);

		ReleaseDC(hwndPanel, dc);
		image->ReleaseAccess(&data);
	}
}

void FaceTrackingRenderer2D::DrawRecognition(PXCFaceData::Face* trackedFace, const int faceId)
{
	PXCFaceData::RecognitionData* recognitionData = trackedFace->QueryRecognition();
	if (recognitionData == NULL)
		return;

	HWND panelWindow = GetDlgItem(m_window, IDC_PANEL);
	HDC dc1 = GetDC(panelWindow);

	if (!dc1)
	{
		return;
	}
	HDC dc2 = CreateCompatibleDC(dc1);
	if (!dc2)
	{
		ReleaseDC(panelWindow, dc1);
		return;
	}

	SelectObject(dc2, m_bitmap);

	BITMAP bitmap;
	GetObject(m_bitmap, sizeof(bitmap), &bitmap);

	HFONT hFont = CreateFont(FaceTrackingUtilities::TextHeight, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 2, 0, L"MONOSPACE");
	SelectObject(dc2, hFont);

	WCHAR line1[64];
	int recognitionID = recognitionData->QueryUserID();
	if (recognitionID != -1)
	{
		swprintf_s<sizeof(line1) / sizeof(pxcCHAR)>(line1, L"Registered ID: %d", recognitionID);
	}
	else
	{
		swprintf_s<sizeof(line1) / sizeof(pxcCHAR)>(line1, L"Not Registered");
	}
	PXCRectI32 rect;
	memset(&rect, 0, sizeof(rect));
	int yStartingPosition;
	if (trackedFace->QueryDetection())
	{
		SetBkMode(dc2, TRANSPARENT);
		trackedFace->QueryDetection()->QueryBoundingRect(&rect);
		yStartingPosition = rect.y;
	}
	else
	{
		const int yBasePosition = bitmap.bmHeight - FaceTrackingUtilities::TextHeight;
		yStartingPosition = yBasePosition - faceId * FaceTrackingUtilities::TextHeight;
		WCHAR userLine[64];
		swprintf_s<sizeof(userLine) / sizeof(pxcCHAR)>(userLine, L" User: %d", faceId);
		wcscat_s(line1, userLine);
	}
	SIZE textSize;
	GetTextExtentPoint32(dc2, line1, std::char_traits<wchar_t>::length(line1), &textSize);
	int x = rect.x + rect.w + 1;
	if (x + textSize.cx > bitmap.bmWidth)
		x = rect.x - 1 - textSize.cx;

	TextOut(dc2, x, yStartingPosition, line1, std::char_traits<wchar_t>::length(line1));

	DeleteDC(dc2);
	ReleaseDC(panelWindow, dc1);
	DeleteObject(hFont);
}

void FaceTrackingRenderer2D::DrawExpressions(PXCFaceData::Face* trackedFace, const int faceId)
{
	PXCFaceData::ExpressionsData* expressionsData = trackedFace->QueryExpressions();
	if (!expressionsData)
		return;

	HWND panelWindow = GetDlgItem(m_window, IDC_PANEL);
	HDC dc1 = GetDC(panelWindow);
	HDC dc2 = CreateCompatibleDC(dc1);
	if (!dc2)
	{
		ReleaseDC(panelWindow, dc1);
		return;
	}

	SelectObject(dc2, m_bitmap);
	BITMAP bitmap;
	GetObject(m_bitmap, sizeof(bitmap), &bitmap);

	HPEN cyan = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));

	if (!cyan)
	{
		DeleteDC(dc2);
		ReleaseDC(panelWindow, dc1);
		return;
	}
	SelectObject(dc2, cyan);

	const int maxColumnDisplayedFaces = 5;
	const int widthColumnMargin = 570;
	const int rowMargin = FaceTrackingUtilities::TextHeight;
	const int yStartingPosition = faceId % maxColumnDisplayedFaces * m_expressionMap.size() * FaceTrackingUtilities::TextHeight;
	const int xStartingPosition = widthColumnMargin * (faceId / maxColumnDisplayedFaces);

	WCHAR tempLine[200];
	int yPosition = yStartingPosition;
	swprintf_s<sizeof(tempLine) / sizeof(pxcCHAR)>(tempLine, L"ID: %d", trackedFace->QueryUserID());
	TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));
	yPosition += rowMargin;

	for (auto expressionIter = m_expressionMap.begin(); expressionIter != m_expressionMap.end(); expressionIter++)
	{
		PXCFaceData::ExpressionsData::FaceExpressionResult expressionResult;
		if (expressionsData->QueryExpression(expressionIter->first, &expressionResult))
		{
			int intensity = expressionResult.intensity;

			if (expressionIter->first == PXCFaceData::ExpressionsData::EXPRESSION_EYES_CLOSED_LEFT) {
				globalLeftEyeIntensity = expressionResult.intensity;
			}
			if (expressionIter->first == PXCFaceData::ExpressionsData::EXPRESSION_TONGUE_OUT) {
				globalTongueOutIntensity = expressionResult.intensity;
			}
			//PXCFaceData::ExpressionsData::EXPRESSION_EYES_CLOSED_RIGHT


			std::wstring expressionName = expressionIter->second;
			swprintf_s<sizeof(tempLine) / sizeof(WCHAR)>(tempLine, L"%s = %d", expressionName.c_str(), intensity);
			TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));
			yPosition += rowMargin;
		}
	}

	DeleteObject(cyan);
	DeleteDC(dc2);
	ReleaseDC(panelWindow, dc1);
}

void FaceTrackingRenderer2D::DrawPoseAndPulse(PXCFaceData::Face* trackedFace, const int faceId)
{
	const PXCFaceData::PoseData* poseData = trackedFace->QueryPose();
	pxcBool poseAnglesExist;
	PXCFaceData::PoseEulerAngles angles;

	if (poseData == NULL)
		poseAnglesExist = 0;
	else
		poseAnglesExist = poseData->QueryPoseAngles(&angles);

	HWND panelWindow = GetDlgItem(m_window, IDC_PANEL);
	HDC dc1 = GetDC(panelWindow);
	HDC dc2 = CreateCompatibleDC(dc1);
	if (!dc2)
	{
		ReleaseDC(panelWindow, dc1);
		return;
	}

	SelectObject(dc2, m_bitmap);
	BITMAP bitmap;
	GetObject(m_bitmap, sizeof(bitmap), &bitmap);
	HPEN cyan = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));

	if (!cyan)
	{
		DeleteDC(dc2);
		ReleaseDC(panelWindow, dc1);
		return;
	}

	SelectObject(dc2, cyan);

	const int maxColumnDisplayedFaces = 5;
	const int widthColumnMargin = 570;
	const int rowMargin = FaceTrackingUtilities::TextHeight;
	const int yStartingPosition = 20 + faceId % maxColumnDisplayedFaces * 6 * FaceTrackingUtilities::TextHeight;
	const int xStartingPosition = bitmap.bmWidth - 100 - widthColumnMargin * (faceId / maxColumnDisplayedFaces);

	WCHAR tempLine[64];
	int yPosition = yStartingPosition;
	swprintf_s<sizeof(tempLine) / sizeof(pxcCHAR)>(tempLine, L"ID: %d", trackedFace->QueryUserID());
	TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

	if (poseAnglesExist) {

		if (poseData->QueryConfidence() > 0) {
			SetTextColor(dc2, RGB(0, 0, 0));
		}
		else {
			SetTextColor(dc2, RGB(255, 0, 0));
		}

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"Yaw : %.0f", angles.yaw);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"Pitch: %.0f", angles.pitch);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"Roll : %.0f ", angles.roll);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

		// print gaze point values

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"Gaze X : %d ", eye_point_x);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"Gaze Y : %d ", eye_point_y);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"VertANgle : %llf ", eye_point_angle_vertical);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));

		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"HoriAngle: %llf", eye_point_angle_horizontal);
		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));


// ------------------ PSU TEAM CODE CHANGES START HERE ------------------ //

		// Choose mode: (eye vs head)
		// ------------------ Track average eye points ------------------ //
		//xAvg[avgIdx] = eye_point_x;
		//yAvg[avgIdx] = eye_point_y;

		//int avgX = 0, avgY = 0;
		//for (int i = 0; i < NUM_OF_AVGS; ++i) {
		//	avgX += xAvg[i];
		//	avgY += yAvg[i];
		//}
		//avgX /= NUM_OF_AVGS;
		//avgY /= NUM_OF_AVGS;


		//int dx = abs(avgX - eye_point_x);
		//int dy = abs(avgY - eye_point_y);
		//double eyeDistance = sqrt(dx*dx + dy*dy);

		//// ------------------ Track average head pose ------------------ //
		//yawAvg[avgIdx] = angles.yaw;
		//pitchAvg[avgIdx] = angles.pitch;
		//rollAvg[avgIdx] = angles.roll;

		//double avgYaw = 0, avgPitch = 0, avgRoll = 0;
		//for (int i = 0; i < NUM_OF_AVGS; ++i) {
		//	avgYaw += yawAvg[i];
		//	avgPitch += pitchAvg[i];
		//	avgRoll += rollAvg[i];
		//}
		//avgYaw /= NUM_OF_AVGS;
		//avgPitch /= NUM_OF_AVGS;
		//avgRoll /= NUM_OF_AVGS;

		//double dYaw = abs(avgYaw - angles.yaw);
		//double dPitch = abs(avgPitch - angles.pitch);
		//double dRoll = abs(avgRoll - angles.roll);
		//double headDistance = sqrt(dYaw*dYaw + dPitch*dPitch + dRoll*dRoll);


		//// ---------------------------- Update average index --------------------------------- //
		//if (avgIdx == NUM_OF_AVGS) {
		//	avgIdx = -1;
		//}
		//avgIdx++;


		//// Temporarily present for debugging //
		char str[256];
		///*sprintf_s(str, "avg pt: (%d,%d)\n", avgX, avgY);
		//OutputDebugStringA(str);
		//sprintf_s(str, "cur eye pt: (%d,%d)\n", eye_point_x, eye_point_y);
		//OutputDebugStringA(str);
		//sprintf_s(str, "avg->cur dist: %d\n", eyeDistance);*/
		/////////////////////////////////////////


		//// ------------------------- Determine dwell time ------------------------- //
		//if (abs(eye_point_x - x_previous) < HEAD_THRESHOLD && abs(eye_point_y - y_previous) < HEAD_THRESHOLD) {
		//	dwellTime++;
		//}
		//else {
		//	dwellTime = 0;
		//	x_previous = eye_point_x;
		//	y_previous = eye_point_y;
		//}
		//// Determine tongue out count
		//if (globalTongueOutIntensity == 100) {
		//	tongueOutCounter += 1;
		//}


		// ----------------------- Determine head vs eye mode ----------------------- //
		POINT lpPoint;
		GetCursorPos(&lpPoint);

		//if (true) {
		//	if (eyeDistance <= 100 && dwellTime > 30) {
		//		eyeMode = false;
		//		headPointX = lpPoint.x;
		//		headPointY = lpPoint.y;
		//		dwellTime = 0;
		//	}
		//}
		//else {
		//	if (abs(lpPoint.x - headPointX) > HEAD_THRESHOLD || abs(lpPoint.y - headPointY) > HEAD_THRESHOLD || tongueOutCounter > 25) {
		//		//eyeMode = true;
		//		tongueOutCounter = 0;
		//	}
		//}


		// ----------------------- Set cursor pos ----------------------- //
		int incX = angles.yaw, incY = angles.pitch;
		if (true) {
			if (abs(incX) > MOUSE_INC_THRESHOLD || abs(incY) > MOUSE_INC_THRESHOLD) {
				if (abs(incX) < MOUSE_INC_THRESHOLD) {
					incX = 0;
				}
				else {
					if (incX <= 0) {
						incX += MOUSE_INC_THRESHOLD;
					}
					else {
						incX -= MOUSE_INC_THRESHOLD;
					}
				}
				if (abs(incY) < MOUSE_INC_THRESHOLD) {
					incY = 0;
				}
				else {
					if (incY <= 0) {
						incY += MOUSE_INC_THRESHOLD;
					}
					else {
						incY -= MOUSE_INC_THRESHOLD;
					}
				}
				SetCursorPos(lpPoint.x + incX, lpPoint.y - incY);

			}
		}
		else {
			SetCursorPos(eye_point_x, eye_point_y);
		}


		// ----------------------- Eye angle code ---------------------- //
		/*double x_position, y_position;

		x_position = (eye_point_angle_horizontal + 30) * (2160 - 0) / (30 + 30) + 0;
		y_position = (eye_point_angle_vertical + 30) * (1440 - 0) / (30 + 30) + 0;



		int x = (((eye_point_angle_horizontal + 60)*(2160 - 0)) / (60 + 60)) + 0;
		int y = (((eye_point_angle_vertical + 60)*(1440 - 0)) / (60 + 60)) + 0;
		SetCursorPos(x_position, y_position);
		*/
		// ------------------------------------------------------------- //


		// ----------------------- Expose pupil position -------------------------- //
		// POINT pupilPt = ExposePupil(trackedFace);
		// ------------------------------------------------------------------------ //
		

		// ---------------------- Clicking ---------------------- //
		if (globalLeftEyeIntensity > 80) {
			INPUT    Input = { 0 };													// Create our input.

			mouseDownCounter++;
			if (mouseDownCounter == 25) {
				mouseDownCounter = 0;

				if (!mouseIsDown) {
					Input.type = INPUT_MOUSE;									// Let input know we are using the mouse.
					Input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;							// We are setting left mouse button down.
					SendInput(1, &Input, sizeof(INPUT));								// Send the input.
					mouseIsDown = true;
					sprintf_s(str, "DOWN\n");
					OutputDebugStringA(str);
				}
				else {
					Input.type = INPUT_MOUSE;									// Let input know we are using the mouse.
					Input.mi.dwFlags = MOUSEEVENTF_LEFTUP;								// We are setting left mouse button up.
					SendInput(1, &Input, sizeof(INPUT));
					mouseIsDown = false;
					sprintf_s(str, "UP\n");
					OutputDebugStringA(str);
				}

			}
		}
		// ------------------------------------------------------ //

	}
	else {
		SetTextColor(dc2, RGB(255, 0, 0));
	}

	const PXCFaceData::PulseData* pulse = trackedFace->QueryPulse();

	if (pulse != NULL) {

		pxcF32 hr = pulse->QueryHeartRate();
		yPosition += rowMargin;
		swprintf_s<sizeof(tempLine) / sizeof(WCHAR) >(tempLine, L"HR: %f", hr);

		TextOut(dc2, xStartingPosition, yPosition, tempLine, std::char_traits<wchar_t>::length(tempLine));
	}

	DeleteObject(cyan);
	DeleteDC(dc2);
	ReleaseDC(panelWindow, dc1);

}

POINT FaceTrackingRenderer2D::ExposePupil(PXCFaceData::Face* trackedFace) {
	POINT landmark = POINT();

	const PXCFaceData::LandmarksData* landmarkData = trackedFace->QueryLandmarks();
	if (landmarkData == NULL) {
		return landmark;
	}

	pxcI32 numPoints = landmarkData->QueryNumPoints();

	landmarkData->QueryPoints(m_landmarkPoints);

	int x, y;
	for (int i = 0; i < numPoints; ++i)
	{
		// ISSUE: This if statement is never true, because every landmark's source alias is always LANDMARK_NOT_NAMED
		if (m_landmarkPoints[i].source.alias == PXCFaceData::LandmarkType::LANDMARK_EYE_RIGHT_CENTER)
		{
			x = (int)m_landmarkPoints[i].image.x + LANDMARK_ALIGNMENT;
			y = (int)m_landmarkPoints[i].image.y + LANDMARK_ALIGNMENT;

			landmark.x = x;
			landmark.y = y;

			break;
		}
	}

	return landmark;

}

void FaceTrackingRenderer2D::DrawLandmark(PXCFaceData::Face* trackedFace)
{
	const PXCFaceData::LandmarksData* landmarkData = trackedFace->QueryLandmarks();
	if (landmarkData == NULL)
		return;

	HWND panelWindow = GetDlgItem(m_window, IDC_PANEL);
	HDC dc1 = GetDC(panelWindow);
	HDC dc2 = CreateCompatibleDC(dc1);

	if (!dc2)
	{
		ReleaseDC(panelWindow, dc1);
		return;
	}

	HFONT hFont = CreateFont(8, 0, 0, 0, FW_LIGHT, 0, 0, 0, 0, 0, 0, 2, 0, L"MONOSPACE");

	if (!hFont)
	{
		DeleteDC(dc2);
		ReleaseDC(panelWindow, dc1);
		return;
	}


	SetBkMode(dc2, TRANSPARENT);

	SelectObject(dc2, m_bitmap);
	SelectObject(dc2, hFont);

	BITMAP bitmap;
	GetObject(m_bitmap, sizeof(bitmap), &bitmap);

	pxcI32 numPoints = landmarkData->QueryNumPoints();
	if (numPoints != m_numLandmarks)
	{
		DeleteObject(hFont);
		DeleteDC(dc2);
		ReleaseDC(panelWindow, dc1);
		return;
	}

	landmarkData->QueryPoints(m_landmarkPoints);
	for (int i = 0; i < numPoints; ++i)
	{
		int x = (int)m_landmarkPoints[i].image.x + LANDMARK_ALIGNMENT;
		int y = (int)m_landmarkPoints[i].image.y + LANDMARK_ALIGNMENT;
		if (m_landmarkPoints[i].confidenceImage)
		{
			//output landmarks around eyes all green
			if ((i >= 10 && i <= 17) || (i >= 18 && i <= 25)) {
				SetTextColor(dc2, RGB(0, 255, 0));
				TextOut(dc2, x, y, L"+++", 1);
			}
			// point #77 is the right eye, point #76 is the left eye
			else if (i == 77 || i == 76) {
				SetTextColor(dc2, RGB(0, 0, 255));
				TextOut(dc2, x, y, L"+++", 1);
			}
			else {
				SetTextColor(dc2, RGB(255, 255, 255));
				TextOut(dc2, x, y, L"•", 1);
			}
		}
		else
		{
			SetTextColor(dc2, RGB(255, 0, 0));
			TextOut(dc2, x, y, L"x", 1);
		}
	}

	DeleteObject(hFont);
	DeleteDC(dc2);
	ReleaseDC(panelWindow, dc1);
}

void FaceTrackingRenderer2D::DrawLocation(PXCFaceData::Face* trackedFace)
{
	const PXCFaceData::DetectionData* detectionData = trackedFace->QueryDetection();
	if (detectionData == NULL)
		return;

	HWND panelWindow = GetDlgItem(m_window, IDC_PANEL);
	HDC dc1 = GetDC(panelWindow);
	HDC dc2 = CreateCompatibleDC(dc1);

	if (!dc2)
	{
		ReleaseDC(panelWindow, dc1);
		return;
	}

	SelectObject(dc2, m_bitmap);

	BITMAP bitmap;
	GetObject(m_bitmap, sizeof(bitmap), &bitmap);

	HPEN cyan = CreatePen(PS_SOLID, 3, RGB(255, 255, 0));

	if (!cyan)
	{
		DeleteDC(dc2);
		ReleaseDC(panelWindow, dc1);
		return;
	}
	SelectObject(dc2, cyan);

	PXCRectI32 rectangle;
	pxcBool hasRect = detectionData->QueryBoundingRect(&rectangle);
	if (!hasRect)
	{
		DeleteObject(cyan);
		DeleteDC(dc2);
		ReleaseDC(panelWindow, dc1);
		return;
	}

	MoveToEx(dc2, rectangle.x, rectangle.y, 0);
	LineTo(dc2, rectangle.x, rectangle.y + rectangle.h);
	LineTo(dc2, rectangle.x + rectangle.w, rectangle.y + rectangle.h);
	LineTo(dc2, rectangle.x + rectangle.w, rectangle.y);
	LineTo(dc2, rectangle.x, rectangle.y);

	WCHAR line[64];
	swprintf_s<sizeof(line) / sizeof(pxcCHAR)>(line, L"%d", trackedFace->QueryUserID());
	TextOut(dc2, rectangle.x, rectangle.y, line, std::char_traits<wchar_t>::length(line));
	DeleteObject(cyan);

	DeleteDC(dc2);
	ReleaseDC(panelWindow, dc1);
}