import requests
import time
import random

# 요청을 보낼 URL
url = 'http://192.168.142.138:7579/tinyiot1/TinyIoT1/testAE1/testCNT1'

# 요청 헤더 설정
headers = {
    'Accept': 'application/json;ty=4',
    'X-M2M-RI': '12345',
    'X-M2M-Origin': 'CAdmin',
    'Content-Type': 'application/json;ty=4',
    'Connection': 'keep-alive',
    'Accept-Encoding': 'gzip, deflate, br',
    'Accept': '*/*'
}

while True:
    # 사용자가 원하는 값을 무작위로 생성
    desired_value = str(random.randint(1, 100))  # 예시로 1부터 100 사이의 무작위 정수 생성

    # POST 요청에 포함할 데이터
    data = {
        "m2m:cin": {
            "con": desired_value
        }
    }

    # POST 요청 보내기
    response = requests.post(url, json=data, headers=headers)

    # 응답 상태 코드 출력
    print(f'Status Code: {response.status_code}')

    # 응답 본문 출력
    print('Response JSON:', response.json())

    # 10초 대기
    time.sleep(5)
