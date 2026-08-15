# {{SUBSYSTEM_TITLE}}

> Document status: Draft
> Baseline: Not established
> Last reviewed: Not reviewed

## 목적과 범위

이 subsystem이 해결하는 문제와 주 독자를 설명한다.

## 현재 제공 범위

현재 코드에서 확인할 수 있는 책임을 적는다.

## 제외 범위

이 subsystem이 소유하지 않는 책임을 적는다.

## 주요 building block과 의존 방향

```text
upstream
-> subsystem
-> downstream
```

## Interface와 invariant

Caller가 알아야 하는 contract, ordering과 failure를 적는다.

## Ownership과 lifetime

Mutable state owner, 생성, 사용, close/teardown와 파괴 순서를 적는다.

## 대표 runtime scenario

- 첫 정상 흐름
- 중요한 failure 또는 teardown 흐름

## 관련 구현과 테스트

- code 위치
- behavioral test 위치

## 지원 범위와 제약

현재 제공 범위 밖의 동작과 독자가 알아야 하는 제약을 사실대로 구분한다.

## 관련 결정

공개 가능한 ADR와 결정 문서를 연결한다.
