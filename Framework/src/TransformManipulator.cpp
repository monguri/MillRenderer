#include "TransformManipulator.h"

using namespace DirectX::SimpleMath;

namespace
{
	float Cos(float rad)
	{
		if (abs(rad) < FLT_EPSILON)
		{
			return 1.0f;
		}

		return cosf(rad);
	}

	float Sin(float rad)
	{
		if (abs(rad) < FLT_EPSILON)
		{
			return 0.0f;
		}

		return sinf(rad);
	}

	float CalcAngle(float sine, float cosine)
	{
		float result = asinf(sine);
		if (cosine < 0.0f)
		{
			result = DirectX::XM_PI - result;
		}

		return result;
	}

	void ToAngle
	(
		const Vector3& v,
		float* angleH,
		float* angleV,
		float* dist
	)
	{
		if (dist != nullptr)
		{
			*dist = v.Length();
		}

		Vector3 src(-v.x, 0.0f, -v.z);
		Vector3 dst = src;

		if (angleH != nullptr)
		{
			if (fabs(src.x) > FLT_EPSILON || fabs(src.z) > FLT_EPSILON)
			{
				src.Normalize(dst);
			}

			*angleH = CalcAngle(dst.x, dst.z);
		}

		if (angleV != nullptr)
		{
			float d = src.Length();
			src.x = d;
			src.y = -v.y;
			src.z = 0.0f;

			if (fabs(src.x) > FLT_EPSILON || fabs(src.y) > FLT_EPSILON)
			{
				src.Normalize(dst);
			}

			*angleV = CalcAngle(dst.y, dst.x);
		}
	}

	void ToVector
	(
		float angleH,
		float angleV,
		Vector3* forward,
		Vector3* upward
	)
	{
		float sx = Sin(angleH);
		float cx = Cos(angleH);
		float sy = Sin(angleV);
		float cy = Cos(angleV);

		if (forward != nullptr)
		{
			forward->x = -cy * sx;
			forward->y = -sy;
			forward->z = -cy * cx;
		}

		if (upward != nullptr)
		{
			upward->x = -sy * sx;
			upward->y = cy;
			upward->z = -sy * cx;
		}
	}
}

void TransformManipulator::UpdateByEvent(const Event& value)
{
	if (value.Type & EventRotate)
	{
		Rotate(value.RotateH, value.RotateV);
	}

	if (value.Type & EventDolly)
	{
		Dolly(value.Dolly);
	}

	if (value.Type & EventMove)
	{
		Move(value.MoveX, value.MoveY, value.MoveZ);
	}

	Update();
}

void TransformManipulator::Update()
{
	if (m_DirtyFlag == DirtyNone)
	{
		return;
	}

	if (m_DirtyFlag & DirtyPosition)
	{
		ComputePosition();
	}

	m_View = Matrix::CreateLookAt
	(
		m_Current.Position,
		m_Current.Target,
		m_Current.Upward
	);

	m_DirtyFlag = DirtyNone;
}

void TransformManipulator::Reset(const Vector3& position, const Vector3& target)
{
	m_Current.Position = position;
	m_Current.Target = target;
	// m_Current‚Ì‘¼‚Ì’l‚ðŒvŽZ‚·‚é
	ComputeAngle();

	m_DirtyFlag = DirtyMatrix;
	Update();

	m_Preserve = m_Current;
}

const Vector3& TransformManipulator::GetPosition() const
{
	return m_Current.Position;
}

const Matrix& TransformManipulator::GetView() const
{
	return m_View;
}

void TransformManipulator::Rotate(float angleH, float angleV)
{
	ComputeAngle();
	ComputeTarget();

	m_Current.Angle.x += angleH;
	m_Current.Angle.y += angleV;

	// Avoid gimbal lock.
	{
		if (m_Current.Angle.y > DirectX::XM_PIDIV2 - FLT_EPSILON)
		{
			m_Current.Angle.y = DirectX::XM_PIDIV2 - FLT_EPSILON;
		}

		if (m_Current.Angle.y < -DirectX::XM_PIDIV2 + FLT_EPSILON)
		{
			m_Current.Angle.y = -DirectX::XM_PIDIV2 + FLT_EPSILON;
		}
	}

	m_DirtyFlag |= DirtyPosition;
}

void TransformManipulator::Move(float moveX, float moveY, float moveZ)
{
	const Vector3& translate = m_View.Right() * moveX + m_View.Up() * moveY	+ m_View.Forward() * moveZ;

	m_Current.Position += translate;
	m_Current.Target += translate;

	m_DirtyFlag |= DirtyMatrix;
}

void TransformManipulator::Dolly(float value)
{
	ComputeAngle();
	ComputeTarget();

	m_Current.Distance += value;
	if (m_Current.Distance < 0.001f)
	{
		m_Current.Distance = 0.001f;
	}

	m_DirtyFlag |= DirtyPosition;
}

void TransformManipulator::ComputePosition()
{
	ToVector(m_Current.Angle.x, m_Current.Angle.y, &m_Current.Forward, &m_Current.Upward);
	m_Current.Position = m_Current.Target - m_Current.Distance * m_Current.Forward;
}

void TransformManipulator::ComputeTarget()
{
	ToVector(m_Current.Angle.x, m_Current.Angle.y, &m_Current.Forward, &m_Current.Upward);
	m_Current.Target = m_Current.Position + m_Current.Distance * m_Current.Forward;
}

void TransformManipulator::ComputeAngle()
{
	m_Current.Forward = m_Current.Target - m_Current.Position;
	// m_Current.Forward‚ð³‹K‰»‚µ‚Ä‚È‚¢‚Ì‚Å’·‚³‚ªm_Current.Distance‚Æ‚È‚é
	ToAngle(m_Current.Forward, &m_Current.Angle.x, &m_Current.Angle.y, &m_Current.Distance);
	// m_Current.Forward‚Í‚±‚±‚Å³‹K‰»‚³‚ê‚é
	ToVector(m_Current.Angle.x, m_Current.Angle.y, &m_Current.Forward, &m_Current.Upward);
}