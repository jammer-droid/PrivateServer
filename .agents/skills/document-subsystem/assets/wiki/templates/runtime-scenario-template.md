# <Runtime Scenario>

> Document status: Draft
> Baseline: Not established
> Last reviewed: Not reviewed

## 문제

이 scenario가 설명해야 하는 동작과 위험을 적는다.

## 시작 조건과 입력

- 시작 상태
- 구체 입력 예시

## 참여 building block

- 참여 component와 역할

## 실행 순서

```text
input
-> step
-> observable result
```

## Ownership과 lifetime 이동

각 단계에서 state, payload와 handle의 owner가 어떻게 바뀌는지 적는다.

## Failure와 pressure 분기

실패 시 보존해야 하는 invariant와 외부 결과를 적는다.

## 외부 관측 결과

Caller가 받는 status, event와 state transition을 적는다.

## 관련 구현과 테스트

| 단계 | 구현 | 관련 테스트 |
| --- | --- | --- |

## 설계 이유

이 실행 구조가 해결하는 문제와 선택한 trade-off를 설명한다.

## 지원 범위와 제약

독자가 이 scenario를 적용하거나 해석할 때 알아야 하는 현재 제약을 적는다.
