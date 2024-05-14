# 朝晖猫砂盆项目

本仓库是朝晖电动猫砂盆的总仓库，子模块包括：
- 硬件设计图纸
- 产品设计图纸
- 嵌入式控制代码
- 上位机代码

## Git教程
1. 廖雪峰的文档: https://www.liaoxuefeng.com/wiki/896043488029600
2. Git培训视频教程TBD.

## 贡献
1. 仅朝晖网络科技公司的合作者可以贡献代码与原理图
2. 请先Fork本仓库，然后在本地进行修改，然后提交Pull Request
3. PR请**务必**提交到**develop分支**，版本确定更新后由管理员merge到main分支
### 添加子模块
- 我手里有代码、设计图，要向项目添加模块
```shell
git submodule add https://github.com/<username>/<repo> <path>
```

- 我想要拿到别人的代码、设计图
```shell
git submodule update --init --recursive
```
