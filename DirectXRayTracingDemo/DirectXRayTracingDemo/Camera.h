#pragma once
#include "stdafx.h"
#include "Externals/GLM/glm/glm.hpp"
#include "Externals/GLM/glm\gtx\rotate_vector.hpp"
#include "Externals/GLM/glm/gtx/transform.hpp"
#include "Externals/GLM/glm/gtc/quaternion.hpp"
#include "Externals/GLM/glm/gtx/quaternion.hpp"
#include "Externals/GLM/glm/gtx/euler_angles.hpp"
#include "Externals/GLM/glm/gtc/matrix_transform.hpp"
#include "Externals/GLM/glm/gtc/type_ptr.hpp"

#include <chrono>
#include <string>
#include <sstream>
#include <iostream>

class Camera
{
public:
	Camera(int width, int height);
	~Camera();

	void UpdateFreeMovement(float time);
	void UpdateRotation(bool mouseDown, bool overwrite = false);
	void UpdateViewProjectionMatrix();
	void Update();
	void SetValues();
	void StartMouseCapture();
	void UpdateSize(int width, int height);

	glm::vec2 RotationValues = glm::vec2(0.0f,-300.0f);
	bool mMouseDown = false;
	float m_MoveSpeed = 350.0f;
	float m_RotationSpeed = 0.3f;

	std::chrono::steady_clock::time_point start;
	POINT oldCursorPos;
	//Camera
	XMFLOAT4 mRotation = XMFLOAT4(0.0f, 0.0f, 0.0f,0.0f);
	//glm::vec3 m_CameraPos = glm::vec3(0.0f, 2.2f, -16.0f);
	glm::vec3 m_CameraPos = glm::vec3(1106.96f, 221.62f, -37.22f);
	glm::vec3 m_CameraPosDelta = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 m_CameraLookAt = glm::vec3(0.0f, 0.0f, 0.1f);
	glm::vec3 m_CameraDirection = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 m_CamFront = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 mForward = glm::vec3(0.0f, 0.0f, 1.0f);

	int mWidth;
	int mHeight;
	float m_CamPitch = 0.0f;
	float m_CamHeading = 0.0f;
	bool m_Moved = false;
	float mTime = 0.0f;

	XMFLOAT3 mForwardFloat;
	XMFLOAT3 mUpFloat;
	glm::mat4 mProjMatrix;
	XMFLOAT4X4 m_View, m_Projection, m_ViewInverse, m_ViewProjection, m_ViewProjectionInverse;
	const glm::mat4& GetProjMatrix() const { return mProjMatrix; }
	const XMFLOAT4X4 GetViewProjMatrix() const { return m_ViewProjection; }
	const XMFLOAT4X4 GetViewProjInverseMatrix() const { return m_ViewProjectionInverse; }
};

