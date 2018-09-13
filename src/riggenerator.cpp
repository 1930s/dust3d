#include <QGuiApplication>
#include <QDebug>
#include <QElapsedTimer>
#include "riggenerator.h"
#include "autorigger.h"

RigGenerator::RigGenerator(const MeshResultContext &meshResultContext) :
    m_meshResultContext(new MeshResultContext(meshResultContext))
{
}

RigGenerator::~RigGenerator()
{
    delete m_meshResultContext;
    delete m_resultMesh;
}

MeshLoader *RigGenerator::takeResultMesh()
{
    MeshLoader *resultMesh = m_resultMesh;
    m_resultMesh = nullptr;
    return resultMesh;
}

void RigGenerator::process()
{
    QElapsedTimer countTimeConsumed;
    countTimeConsumed.start();
    
    std::vector<QVector3D> inputVerticesPositions;
    std::set<MeshSplitterTriangle> inputTriangles;
    
    for (const auto &vertex: m_meshResultContext->vertices) {
        inputVerticesPositions.push_back(vertex.position);
    }
    std::map<std::pair<SkeletonBoneMark, SkeletonSide>, std::tuple<QVector3D, int, std::set<MeshSplitterTriangle>>> marksMap;
    for (size_t triangleIndex = 0; triangleIndex < m_meshResultContext->triangles.size(); triangleIndex++) {
        const auto &sourceTriangle = m_meshResultContext->triangles[triangleIndex];
        MeshSplitterTriangle newTriangle;
        for (int i = 0; i < 3; i++)
            newTriangle.indicies[i] = sourceTriangle.indicies[i];
        auto findBmeshNodeResult = m_meshResultContext->bmeshNodeMap().find(m_meshResultContext->triangleSourceNodes()[triangleIndex]);
        if (findBmeshNodeResult != m_meshResultContext->bmeshNodeMap().end()) {
            const auto &bmeshNode = *findBmeshNodeResult->second;
            if (bmeshNode.boneMark != SkeletonBoneMark::None) {
                SkeletonSide boneSide = SkeletonSide::None;
                if (SkeletonBoneMarkHasSide(bmeshNode.boneMark)) {
                    boneSide = bmeshNode.origin.x() > 0 ? SkeletonSide::Left : SkeletonSide::Right;
                }
                auto &marks = marksMap[std::make_pair(bmeshNode.boneMark, boneSide)];
                std::get<0>(marks) += bmeshNode.origin;
                std::get<1>(marks) += 1;
                std::get<2>(marks).insert(newTriangle);
            }
        }
        inputTriangles.insert(newTriangle);
    }
    AutoRigger autoRigger(inputVerticesPositions, inputTriangles);
    for (const auto &marks: marksMap) {
        autoRigger.addMarkGroup(marks.first.first, marks.first.second,
            std::get<0>(marks.second) / std::get<1>(marks.second),
            std::get<2>(marks.second));
    }
    bool rigSucceed = autoRigger.rig();
    
    if (rigSucceed) {
        qDebug() << "Rig succeed";
    } else {
        qDebug() << "Rig failed";
        for (const auto &message: autoRigger.messages()) {
            qDebug() << message.first << message.second;
        }
    }
    
    // Blend vertices colors according to bone weights
    
    std::vector<QColor> inputVerticesColors(m_meshResultContext->vertices.size());
    if (rigSucceed) {
        const auto &resultWeights = autoRigger.resultWeights();
        const auto &resultBones = autoRigger.resultBones();
        for (size_t vertexIndex = 0; vertexIndex < inputVerticesColors.size(); vertexIndex++) {
            auto findResult = resultWeights.find((int)vertexIndex);
            int blendR = 0, blendG = 0, blendB = 0;
            if (findResult != resultWeights.end()) {
                for (int i = 0; i < 4; i++) {
                    int boneIndex = findResult->second.boneIndicies[i];
                    if (boneIndex > 0) {
                        const auto &bone = resultBones[boneIndex];
                        blendR += bone.color.red() * findResult->second.boneWeights[i];
                        blendG += bone.color.green() * findResult->second.boneWeights[i];
                        blendB += bone.color.blue() * findResult->second.boneWeights[i];
                    }
                }
            }
            QColor blendColor = QColor(blendR, blendG, blendB, 255);
            inputVerticesColors[vertexIndex] = blendColor;
        }
    }
    
    // Smooth normals
    
    std::map<int, QVector3D> vertexNormalMap;
    for (size_t triangleIndex = 0; triangleIndex < m_meshResultContext->triangles.size(); triangleIndex++) {
        const auto &sourceTriangle = m_meshResultContext->triangles[triangleIndex];
        for (int i = 0; i < 3; i++)
            vertexNormalMap[sourceTriangle.indicies[i]] += sourceTriangle.normal;
    }
    for (auto &item: vertexNormalMap)
        item.second.normalize();
    
    // Create mesh for demo
    
    Vertex *triangleVertices = new Vertex[m_meshResultContext->triangles.size() * 3];
    int triangleVerticesNum = 0;
    for (size_t triangleIndex = 0; triangleIndex < m_meshResultContext->triangles.size(); triangleIndex++) {
        const auto &sourceTriangle = m_meshResultContext->triangles[triangleIndex];
        for (int i = 0; i < 3; i++) {
            Vertex &currentVertex = triangleVertices[triangleVerticesNum++];
            const auto &sourcePosition = inputVerticesPositions[sourceTriangle.indicies[i]];
            const auto &sourceColor = inputVerticesColors[sourceTriangle.indicies[i]];
            const auto &sourceNormal = vertexNormalMap[sourceTriangle.indicies[i]];
            currentVertex.posX = sourcePosition.x();
            currentVertex.posY = sourcePosition.y();
            currentVertex.posZ = sourcePosition.z();
            currentVertex.texU = 0;
            currentVertex.texV = 0;
            currentVertex.colorR = sourceColor.redF();
            currentVertex.colorG = sourceColor.greenF();
            currentVertex.colorB = sourceColor.blueF();
            currentVertex.normX = sourceNormal.x();
            currentVertex.normY = sourceNormal.y();
            currentVertex.normZ = sourceNormal.z();
        }
    }
    m_resultMesh = new MeshLoader(triangleVertices, triangleVerticesNum);
    
    qDebug() << "The rig generation took" << countTimeConsumed.elapsed() << "milliseconds";
    
    this->moveToThread(QGuiApplication::instance()->thread());
    emit finished();
}
