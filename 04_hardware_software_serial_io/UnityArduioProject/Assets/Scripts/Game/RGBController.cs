using UnityEngine;
using System.Collections.Generic;

public class RGBController : MonoBehaviour
{
    [SerializeField] private RenderTexture _renderTexture;

    [Header("Black Filtering")]
    [SerializeField] private float _blackThreshold = 0.05f;
    [SerializeField] private float _minSaturation = 0.15f;

    [Header("Sampling")]
    [SerializeField] private int _sampleStep = 1;
    [SerializeField] private int _hueBuckets = 36; // 10° per bucket
    [SerializeField] private float _valueMultiplier = 1;

    [Header("Smoothing")]
    [SerializeField] private float _smoothingSpeed = 5f;

    private Texture2D _readTexture;
    private Color _currentLedColor = Color.black;

    void LateUpdate()
    {
        UpdateLEDFromRenderTexture();
    }

    public void UpdateLEDFromRenderTexture()
    {
        if (_renderTexture == null)
            return;

        RenderTexture.active = _renderTexture;

        if (_readTexture == null ||
            _readTexture.width != _renderTexture.width ||
            _readTexture.height != _renderTexture.height)
        {
            _readTexture = new Texture2D(
                _renderTexture.width,
                _renderTexture.height,
                TextureFormat.RGB24,
                false
            );
        }

        _readTexture.ReadPixels(
            new Rect(0, 0, _renderTexture.width, _renderTexture.height),
            0,
            0
        );
        _readTexture.Apply();

        RenderTexture.active = null;

        Color targetColor = GetDominantHSVColor(_readTexture);

        // Temporal smoothing
        _currentLedColor = Color.Lerp(
            _currentLedColor,
            targetColor,
            Time.deltaTime * _smoothingSpeed
        );

        ArduinoInput.Instance.SetRGBLed(
            Mathf.RoundToInt(_currentLedColor.r * 255f),
            Mathf.RoundToInt(_currentLedColor.g * 255f),
            Mathf.RoundToInt(_currentLedColor.b * 255f)
        );
    }

    private Color GetDominantHSVColor(Texture2D tex)
    {
        Color32[] pixels = tex.GetPixels32();

        int[] counts = new int[_hueBuckets];
        float[] satSum = new float[_hueBuckets];
        float[] valSum = new float[_hueBuckets];

        for (int i = 0; i < pixels.Length; i += _sampleStep)
        {
            Color c = pixels[i];

            Color.RGBToHSV(c, out float h, out float s, out float v);

            // Exclude black & gray
            if (v < _blackThreshold || s < _minSaturation)
                continue;

            int bucket = Mathf.FloorToInt(h * _hueBuckets);
            bucket = Mathf.Clamp(bucket, 0, _hueBuckets - 1);

            counts[bucket]++;
            satSum[bucket] += s;
            valSum[bucket] += v;
        }

        int dominantBucket = -1;
        int maxCount = 0;

        for (int i = 0; i < _hueBuckets; i++)
        {
            if (counts[i] > maxCount)
            {
                maxCount = counts[i];
                dominantBucket = i;
            }
        }

        if (dominantBucket == -1)
            return Color.black;

        float avgHue = (dominantBucket + 0.5f) / _hueBuckets;
        float avgSat = satSum[dominantBucket] / counts[dominantBucket];
        float avgVal = valSum[dominantBucket] * _valueMultiplier / counts[dominantBucket];

        return Color.HSVToRGB(avgHue, avgSat, avgVal);
    }
}