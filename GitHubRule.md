<!-- File: GitHubRule.md -->

# GitHub ルール

## 基本ルール

- `main` ブランチには直接 `push` しない
- 必ず作業用ブランチを作って作業する
- 作業が終わったら Pull Request を作成する
- レビュー後に `main` へ `merge` する
- 作業前には最新の `main` を取り込む

---

## 作業の流れ

### 1. 最新のコードを取り込む

```bash
git checkout main
git pull origin main