#include "stdafx.h"
#include "Camera.h"
#include <Windows.h>
#include <iostream>
#include <chrono>


Camera::Camera(int width, int height)
{
	mWidth = width;
	mHeight = height;
	start = std::chrono::high_resolution_clock::now();
	
	SetValues();	
}

void Camera::StartMouseCapture()
{
	mMouseDown = true;
	POINT p;
	if (GetCursorPos(&p))
	{
		oldCursorPos = p;
	}
}

Camera::~Camera()
{
	
}

void Camera::UpdateFreeMovement(float time)
{
	glm::vec3 oldPos = m_CameraPos;
	if (GetKeyState('W') & 0x8000)
	{
		m_CameraPos += glm::float3(mForwardFloat.x, mForwardFloat.y, mForwardFloat.z ) * m_MoveSpeed * time;
	}


	if (GetKeyState('D') & 0x8000)
	{
		m_CameraPos -= glm::cross(glm::float3(mForwardFloat.x, mForwardFloat.y, mForwardFloat.z), glm::float3(mUpFloat.x, mUpFloat.y, mUpFloat.z)) * m_MoveSpeed * time;
	}


	if (GetKeyState('S') & 0x8000)
	{
		m_CameraPos -= glm::float3(mForwardFloat.x, mForwardFloat.y, mForwardFloat.z) * m_MoveSpeed * time;
	}


	if (GetKeyState('A') & 0x8000)
	{
		m_CameraPos += glm::cross(glm::float3(mForwardFloat.x, mForwardFloat.y, mForwardFloat.z), glm::float3(mUpFloat.x, mUpFloat.y, mUpFloat.z)) * m_MoveSpeed * time;
	}


	if (GetKeyState('E') & 0x8000)
	{
		m_CameraPos += glm::vec3(0.0f, 1.0f, 0.0f) * m_MoveSpeed * time;
	}


	if (GetKeyState('Q') & 0x8000)
	{
		m_CameraPos -= glm::vec3(0.0f, 1.0f, 0.0f) * m_MoveSpeed * time;
	}

	if (oldPos != m_CameraPos)
	{
		m_Moved = true;
	}
}

void Camera::UpdateRotation(bool mouseDown, bool overwrite)
{
	mMouseDown = mouseDown;
	if (mouseDown)
	{
		POINT p;
		if (GetCursorPos(&p))
		{
			if (overwrite)
			{
				oldCursorPos.y = p.y;
				oldCursorPos.x = p.x;
			}
			float cursorDeltaX = p.y - oldCursorPos.y;
			float cursorDeltaY = p.x - oldCursorPos.x;
			RotationValues.x += cursorDeltaX;
			RotationValues.y += cursorDeltaY;
			m_CamPitch = RotationValues.y * m_RotationSpeed;
			m_CamHeading = RotationValues.x * m_RotationSpeed;
			if (true)
			{
				XMStoreFloat4(&mRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(m_CamHeading), XMConvertToRadians(m_CamPitch), XMConvertToRadians(0)));
			}
			else
			{
				XMStoreFloat4(&mRotation, XMQuaternionRotationRollPitchYaw(m_CamHeading, m_CamPitch, 0));
			}
			//Reset cursor pos
			oldCursorPos.y = p.y;
			oldCursorPos.x = p.x;

			if (cursorDeltaY != 0.0f || cursorDeltaX != 0.0f)
			{
				m_Moved = true;
			}
		}
	}
	//std::cout << "m_CamHeading: " << m_CamHeading << " m_CamPitch: " << m_CamPitch << "\n";
}

void Camera::UpdateViewProjectionMatrix()
{
	XMMATRIX projection, view, viewInv, viewProjectionInv;
	float fovAngleY = 45.0f;
	float aspectRatio = static_cast<float>(mWidth) / static_cast<float>(mHeight);

	XMMATRIX rotMat = XMMatrixRotationQuaternion(XMLoadFloat4(&mRotation));
	XMVECTOR forward = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 0), rotMat);
	XMVECTOR right = XMVector3TransformCoord(XMVectorSet(1, 0, 0, 0), rotMat);
	XMStoreFloat3(&mForwardFloat,forward);

	projection = XMMatrixPerspectiveFovLH(fovAngleY, aspectRatio, 1.0f, 125.0f);

	XMVECTOR worldPosition = XMLoadFloat3(new XMFLOAT3(m_CameraPos.x, m_CameraPos.y, m_CameraPos.z));
	XMVECTOR lookAt = forward;
	//XMVECTOR lookAt = XMLoadFloat3(new XMFLOAT3(m_CameraLookAt.x, m_CameraLookAt.y, m_CameraLookAt.z)); //forward
	XMVECTOR up = XMVector3Cross(forward, right); //foward, right
	XMStoreFloat3(&mUpFloat, up);

	view = XMMatrixLookAtLH(worldPosition, worldPosition + lookAt, up);
	//view = XMMatrixLookAtLH(worldPosition, lookAt, up);
	viewInv = XMMatrixInverse(nullptr, view);
	viewProjectionInv = XMMatrixInverse(nullptr, view * projection);

	XMStoreFloat4x4(&m_Projection, projection);
	XMStoreFloat4x4(&m_View, view);
	XMStoreFloat4x4(&m_ViewInverse, viewInv);
	XMStoreFloat4x4(&m_ViewProjection, view * projection);
	XMStoreFloat4x4(&m_ViewProjectionInverse, viewProjectionInv);
}

void Camera::Update()
{
	auto currentTime = std::chrono::high_resolution_clock::now();
	float time = std::chrono::duration<float, std::milli> (currentTime - start).count() / 1000.0f;
	mTime = time;

	m_CameraDirection = normalize(m_CameraLookAt - m_CameraPos);
	if (mMouseDown)
	{
		UpdateRotation(mMouseDown);
	}
	UpdateFreeMovement(time);
	UpdateViewProjectionMatrix();
	//m_CameraDirection = glm::rotateZ(m_CameraDirection, m_CamHeading*-1);	
	//m_CameraDirection = glm::normalize(m_CameraDirection);

	//m_CameraLookAt = m_CameraPos + m_CameraDirection;
	//m_CamPitch = 0.0f;
	//m_CamHeading = 0.0f;
	start = std::chrono::high_resolution_clock::now();
}

void Camera::SetValues()
{
	//Front look
	RotationValues = glm::vec2(0.0f, -300.0f);
	m_CameraPos = glm::vec3(1106.96f, 221.62f, -37.22f);



	//UPDATE Values
	m_Moved = true;
	UpdateRotation(true, true);
	mMouseDown = false;
}

void Camera::UpdateSize(int width, int height)
{
	mWidth = width;
	mHeight = height;
}