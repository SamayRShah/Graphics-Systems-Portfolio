using UnityEngine;

public class CameraSpinner : MonoBehaviour
{
    [SerializeField] private float _turnSpeed = 8;
    // Update is called once per frame
    void Update()
    {
        transform.Rotate(0, 0, _turnSpeed * Time.deltaTime);
    }
}
