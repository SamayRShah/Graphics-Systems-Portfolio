using UnityEngine;
using static UnityEngine.Rendering.HableCurve;

public class KalleidoscopeControlller : MonoBehaviour
{
    [SerializeField] private Material _shader;
    [SerializeField] private float _maxSegments = 24f;

    private float _prevSegments = 1f;
    private void Update()
    {
        float segments = ArduinoInput.Instance.TwistValue * _maxSegments;

        if (segments == _prevSegments) return;
        _prevSegments = segments;
        _shader.SetFloat("_SegmentCount", segments);
    }
}
