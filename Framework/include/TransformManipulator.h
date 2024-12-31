#pragma once
#include <d3d12.h>
#include <SimpleMath.h>

class TransformManipulator
{
public:
	enum EventType
	{
		EventRotate = 0x1 << 0,
		EventDolly = 0x1 << 1,
		EventMove = 0x1 << 2,
	};

	struct Event
	{
		uint32_t Type = 0;
		float RotateH = 0.0f;
		float RotateV = 0.0f;
		float Dolly = 0.0f;
		float MoveX = 0.0f;
		float MoveY = 0.0f;
		float MoveZ = 0.0f;
	};

	void UpdateByEvent(const Event& value);
	void Update();
	void Reset(const DirectX::SimpleMath::Vector3& position, const DirectX::SimpleMath::Vector3& target);

	const DirectX::SimpleMath::Vector3& GetPosition() const;
	const DirectX::SimpleMath::Matrix& GetView() const;

private:
	enum DirtyFlag
	{
		DirtyNone = 0x0,
		DirtyPosition = 0x1 << 1,
		DirtyMatrix = 0x1 << 2,
	};

	struct Param
	{
		DirectX::SimpleMath::Vector3 Position;
		DirectX::SimpleMath::Vector3 Target;
		DirectX::SimpleMath::Vector3 Upward;
		DirectX::SimpleMath::Vector3 Forward;
		DirectX::XMFLOAT2 Angle;
		float Distance;
	};

	Param m_Current = {};
	Param m_Preserve = {};
	DirectX::SimpleMath::Matrix m_View = DirectX::SimpleMath::Matrix::Identity;
	uint32_t m_DirtyFlag = 0;

	void Rotate(float angleH, float angleV);
	void Move(float moveX, float moveY, float moveZ);
	void Dolly(float value);

	void ComputePosition();
	void ComputeTarget();
	void ComputeAngle();
};
