using UnityEngine;

[CreateAssetMenu(fileName = "FloatContainer", menuName = "ScriptableObjects/FloatContainer")]
public class FloatContainer : ScriptableObject
{
    [SerializeField] public float Value;
}
