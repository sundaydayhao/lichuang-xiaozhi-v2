#  生图API开发文档

## 基本信息
```
host:
https://laimiaoai.com

```

## 生成图片接口, post
### /api/apply/generate_image
#### 请求参数：
```json
{
  "api_secret": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1aWQiOjIwMjExMjM0OTY4NDI3NjAxOTIsInRlYW1faWQiOjIwMjExMjM4OTMxMjQ3OTY0MTYsIndvcmtmbG93X2lkIjoyMDIxMTMyOTQxNDA4NzM1MjMyLCJ3b3JrZmxvd19uYW1lIjoiWl9pbWFnZV9cdTY1ODdcdTc1MWZcdTU2ZmUiLCJleHAiOjQxMDI0NDQ3OTl9.ovq_-OdbXWF1n2k0r78rC1Que8uzj02lcT90B8pkdlk",
  "prompt": "画一只老虎在森林中，线条风格",
  "width": 320,
  "height": 320
}
```
#### 响应参数：
```json
{
    "prompt_id": "运行id"
}
```

## 查询文生图查询接口, get
### /api/apply/get_image?prompt_id=xxxxxxx
#### 请求参数:
```
prompt_id, 运行id,string
```
#### 响应参数:
##### 排队中
```json
{
  "status": "pending",
  "message": "任务在队列中"
}
```
##### 完成
```json
{
    "status": "success",
    "message": [
        "https://public.laimiaoai.com/images/2026-03-16/9102508b-ca92-4012-8888-07cae387702b.jpg"
    ]
}
```
