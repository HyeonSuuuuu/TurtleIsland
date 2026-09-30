-- 촌장 퀘스트 (몬스터 처치형). 촌장과 대화하다 수락·보고하면 서버가 처치 수를 세고 보상을 준다.
--   id           : 1 이상, 중복 불가 (저장 데이터가 이 번호를 가리키므로 바꾸지 말 것)
--   title        : 제목 (UTF-8 47바이트 이하, 한글 약 15자)
--   description  : 촌장이 설명할 내용 (240바이트 이하)
--   monster      : 잡을 몬스터 번호 (Monster.lua 순서, 0 부터)
--   count        : 처치 수 / gold, exp : 보상
--   minLevel     : 받을 수 있는 최소 레벨 / prerequisite : 먼저 끝내야 하는 퀘스트 id (없으면 생략)
-- 한 번 완료한 퀘스트는 다시 받지 않는다. 동시에 진행할 수 있는 퀘스트는 3개.
Quests = {
  { id = 1, title = "마을 옆 스토커 퇴치", monster = 0, count = 5, gold = 700, exp = 200, minLevel = 1,
    description = "마을 바로 옆 풀숲에 스토커가 부쩍 늘었네. 다섯 마리만 잡아 주면 마을 사람들이 한시름 놓겠어." },
  { id = 2, title = "늪지의 타르 포드", monster = 1, count = 5, gold = 1000, exp = 400, minLevel = 2, prerequisite = 1,
    description = "늪지에서 타르 포드가 둥지를 넓히고 있네. 다섯 마리를 처치해서 번식을 늦춰 주게." },
  { id = 3, title = "파수꾼의 눈 감기기", monster = 2, count = 3, gold = 1500, exp = 500, minLevel = 3, prerequisite = 2,
    description = "늪지 둥지를 지키는 센트리 아이 때문에 아무도 가까이 못 가네. 세 마리만 쓰러뜨려 주게." },
  { id = 4, title = "크러셔 사냥", monster = 3, count = 3, gold = 900, exp = 2000, minLevel = 5, prerequisite = 3,
    description = "섬 깊숙한 곳의 크러셔가 길을 부수고 다니네. 단단한 놈들이니 장비를 갖추고 세 마리를 잡아 주게." },
  { id = 5, title = "데스웜 토벌", monster = 4, count = 1, gold = 2000, exp = 3000, minLevel = 7, prerequisite = 4,
    description = "섬에서 가장 위험한 데스웜이 땅 밑에서 올라오기 시작했네. 한 마리라도 쓰러뜨리면 섬이 조용해질 걸세." },
}
